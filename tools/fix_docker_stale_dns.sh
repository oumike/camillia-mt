#!/usr/bin/env bash
#
# Find and fix containers whose embedded DNS resolver still forwards to a
# nameserver that no longer exists.
#
# Docker snapshots the host's /etc/resolv.conf when a container is CREATED and
# never re-reads it. If the host's resolvers change afterwards -- a LAN renumber,
# NetworkManager rewriting the file minutes after dockerd started -- every
# container created before that change keeps forwarding to the old address. The
# symptom is nasty because the container looks healthy: it answers on its port,
# container-to-container names still resolve (the embedded resolver handles those
# itself), and only *external* lookups fail. Apps surface that as a hang and then
# a 502, which reads like the upstream API is down rather than like a DNS fault.
#
# Restarting dockerd does NOT fix an already-created container, and neither does
# `docker restart`. The snapshot is taken at create time, so the container has to
# be recreated -- which is what this script does, via its own compose project so
# the recreated container keeps its original definition.
#
# Usage:
#   ./fix_docker_stale_dns.sh                 audit only (default; changes nothing)
#   ./fix_docker_stale_dns.sh --fix           recreate every stale container
#   ./fix_docker_stale_dns.sh --fix NAME...   recreate only the named ones
#   ./fix_docker_stale_dns.sh --verify        audit, then live-test external DNS
#
# Options:
#   --yes        skip the confirmation prompt (for unattended runs)
#   --dry-run    with --fix, print the compose commands instead of running them
#
# Run it on the Docker host. Reading the resolv.conf of a container with no shell
# needs root, so prefer `sudo ./fix_docker_stale_dns.sh` for a complete audit;
# without it those show as "unknown" rather than being silently called healthy.

set -uo pipefail

FIX=0; ASSUME_YES=0; DRY_RUN=0; VERIFY=0; TARGETS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --fix)     FIX=1 ;;
        --verify)  VERIFY=1 ;;
        --yes|-y)  ASSUME_YES=1 ;;
        --dry-run) DRY_RUN=1 ;;
        -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)        echo "unknown option: $1" >&2; exit 2 ;;
        *)         TARGETS+=("$1") ;;
    esac
    shift
done

command -v docker >/dev/null 2>&1 || { echo "docker not found; run this on the Docker host" >&2; exit 1; }

# The resolvers the host would hand a container created right now. Anything a
# container forwards to that is NOT in this list is what we are looking for.
# Probed once rather than per container: the resolv.conf of a container lives
# under /var/lib/docker and is root-owned, so without this the distroless ones
# can only be reported as unknown.
CAN_SUDO=0
if [ "$(id -u)" -eq 0 ]; then CAN_SUDO=1
elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then CAN_SUDO=1
fi

host_servers="$(grep -E '^nameserver' /etc/resolv.conf 2>/dev/null | awk '{print $2}' | tr '\n' ' ')"
[ -n "${host_servers// }" ] || { echo "could not read nameservers from /etc/resolv.conf" >&2; exit 1; }
echo "Host resolvers now: ${host_servers% }"
echo

# The addresses a container would actually forward a lookup to, space separated.
#
# Two shapes to read, and missing the second one is how a stale container hides.
# A container on a docker network resolves through the embedded server at
# 127.0.0.11 and names its real forwarders in an "ExtServers:" comment. One on
# host networking has no embedded resolver at all, so there is no ExtServers
# line anywhere and the plain "nameserver" lines ARE the forwarders. Reading
# only ExtServers reports those as unknown and walks straight past a container
# with a dead resolver in it -- which is exactly what happened to netdata.
#
# 127.0.0.11 is dropped when ExtServers is absent: on a bridge container whose
# comment we could not read it is the embedded stub, not a real forwarder, and
# calling it stale would be a false alarm.
#
# Host-side file first, since that is the only way to read a distroless image
# with no shell to exec into; sudo because it lives under /var/lib/docker.
container_resolvers() {
    local c="$1" path body line
    path="$(docker inspect -f '{{.ResolvConfPath}}' "$c" 2>/dev/null)"
    body=""
    if [ -n "$path" ]; then
        if [ -r "$path" ]; then
            body="$(cat "$path" 2>/dev/null)"
        elif [ "$CAN_SUDO" -eq 1 ]; then
            body="$(sudo -n cat "$path" 2>/dev/null)"
        fi
    fi
    [ -n "$body" ] || body="$(docker exec "$c" cat /etc/resolv.conf 2>/dev/null)"
    [ -n "$body" ] || return 1

    line="$(printf '%s\n' "$body" | grep -o 'ExtServers:.*')"
    if [ -n "$line" ]; then
        printf '%s' "$(printf '%s' "$line" | grep -oE '[0-9]{1,3}(\.[0-9]{1,3}){3}' | tr '\n' ' ')"
        return 0
    fi
    line="$(printf '%s\n' "$body" | awk '$1=="nameserver"{print $2}' \
            | grep -E '^[0-9]{1,3}(\.[0-9]{1,3}){3}$' | grep -v '^127\.0\.0\.11$' | tr '\n' ' ')"
    [ -n "$line" ] || return 1
    printf '%s' "$line"
}

# Stale = forwards to at least one address the host no longer lists. Comparing
# against the live host list rather than a hardcoded bad address keeps this
# correct the next time the network changes to something we cannot predict.
is_stale() {
    local ext="$1" addr
    for addr in $ext; do
        case " $host_servers " in
            *" $addr "*) ;;
            *) return 0 ;;
        esac
    done
    return 1
}

stale=(); unknown=()

printf '%-30s %-8s %s\n' "CONTAINER" "STATE" "FORWARDS TO"
printf '%-30s %-8s %s\n' "------------------------------" "--------" "-----------"
while read -r name; do
    [ -n "$name" ] || continue
    if ! ext="$(container_resolvers "$name")"; then
        printf '%-30s %-8s %s\n' "$name" "unknown" \
               "$([ "$CAN_SUDO" -eq 1 ] && echo "(no resolvers listed)" || echo "(needs root; re-run with sudo)")"
        unknown+=("$name")
        continue
    fi
    addrs="$ext"
    if is_stale "$ext"; then
        printf '%-30s %-8s %s\n' "$name" "STALE" "${addrs% }"
        stale+=("$name")
    else
        printf '%-30s %-8s %s\n' "$name" "ok" "${addrs% }"
    fi
done < <(docker ps --format '{{.Names}}' | sort)

echo
if [ ${#stale[@]} -eq 0 ]; then
    echo "No stale containers found."
    [ ${#unknown[@]} -gt 0 ] && echo "(${#unknown[@]} could not be read without root -- re-run with sudo to be sure.)"
    exit 0
fi
echo "${#stale[@]} container(s) forwarding to a resolver the host no longer lists."

# A live lookup, so we distinguish "misconfigured but coping" from "actually
# broken". Plenty of containers never make an external call and so never notice;
# those do not need a restart and this is what tells them apart.
if [ "$VERIFY" -eq 1 ]; then
    echo
    echo "Live external-DNS check:"
    for c in "${stale[@]}"; do
        if docker exec "$c" getent hosts example.com >/dev/null 2>&1; then
            printf '  %-28s resolves anyway\n' "$c"
        elif docker exec "$c" true 2>/dev/null; then
            printf '  %-28s CANNOT RESOLVE\n' "$c"
        else
            printf '  %-28s (no shell to test with)\n' "$c"
        fi
    done
fi

[ "$FIX" -eq 1 ] || { echo; echo "Audit only. Re-run with --fix to recreate them (or --fix NAME... for specific ones)."; exit 0; }

# Restrict to the names asked for, and reject one that is not actually stale --
# recreating a healthy container is exactly the sort of unnecessary restart this
# script should refuse to do on the user's behalf.
if [ ${#TARGETS[@]} -gt 0 ]; then
    chosen=()
    for want in "${TARGETS[@]}"; do
        ok=0
        for c in "${stale[@]}"; do [ "$c" = "$want" ] && ok=1 && break; done
        if [ "$ok" -eq 1 ]; then chosen+=("$want")
        else echo "skipping '$want': not in the stale list" >&2; fi
    done
    stale=("${chosen[@]}")
    [ ${#stale[@]} -gt 0 ] || { echo "Nothing to do."; exit 0; }
fi

# Recreate through compose so the container comes back with its original
# definition. A container with no compose labels is left alone on purpose:
# rebuilding one by hand from `docker inspect` loses flags and is not something
# to attempt unattended.
echo
echo "Will recreate:"
plan_names=(); plan_cmds=()
for c in "${stale[@]}"; do
    cfg="$(docker inspect -f '{{index .Config.Labels "com.docker.compose.project.config_files"}}' "$c" 2>/dev/null)"
    proj="$(docker inspect -f '{{index .Config.Labels "com.docker.compose.project"}}' "$c" 2>/dev/null)"
    svc="$(docker inspect -f '{{index .Config.Labels "com.docker.compose.service"}}' "$c" 2>/dev/null)"
    wd="$(docker inspect -f '{{index .Config.Labels "com.docker.compose.project.working_dir"}}' "$c" 2>/dev/null)"
    if [ -z "$cfg" ] || [ -z "$svc" ]; then
        echo "  $c -- SKIP (not compose-managed; recreate it by hand)"
        continue
    fi
    # Compose auto-loads ".env" but nothing else, so a project keeping its
    # variables in e.g. "compose.env" needs --env-file or the recreate aborts on
    # an unresolved variable. It fails safely -- compose stops before touching
    # the running container -- but it fails, so find the file and pass it. Only
    # when there is exactly one candidate: guessing between several would be
    # picking someone's production environment at random.
    envopt=""
    if [ -n "$wd" ] && [ ! -f "$wd/.env" ]; then
        mapfile -t envfiles < <(find "$wd" -maxdepth 1 -type f -name '*.env' 2>/dev/null)
        if [ "${#envfiles[@]}" -eq 1 ]; then
            envopt="--env-file $(printf '%q' "${envfiles[0]}") "
            echo "  (using env file ${envfiles[0]##*/} for $c)"
        elif [ "${#envfiles[@]}" -gt 1 ]; then
            echo "  NOTE: $c has ${#envfiles[@]} *.env files; not guessing -- pass --env-file by hand if it fails"
        fi
    fi
    plan_names+=("$c")
    plan_cmds+=("cd $(printf '%q' "${wd:-/}") && docker compose ${envopt}-f $(printf '%q' "$cfg") -p $(printf '%q' "$proj") up -d --force-recreate $(printf '%q' "$svc")")
    echo "  $c  ->  compose project '$proj', service '$svc'"
done
[ ${#plan_names[@]} -gt 0 ] || { echo; echo "Nothing compose-managed to recreate."; exit 0; }

if [ "$DRY_RUN" -eq 1 ]; then
    echo; echo "Dry run; commands that would run:"
    for cmd in "${plan_cmds[@]}"; do echo "  $cmd"; done
    exit 0
fi

if [ "$ASSUME_YES" -ne 1 ]; then
    echo
    printf 'Recreate these %d container(s)? [y/N] ' "${#plan_names[@]}"
    read -r reply </dev/tty 2>/dev/null || reply=""
    case "$reply" in [yY]*) ;; *) echo "Aborted."; exit 1 ;; esac
fi

echo
failed=0
for i in "${!plan_names[@]}"; do
    c="${plan_names[$i]}"
    echo "--- $c"
    if bash -c "${plan_cmds[$i]}"; then
        if ext="$(container_resolvers "$c")" && ! is_stale "$ext"; then
            echo "    OK: now forwards to $(printf '%s' "$ext" | grep -oE '[0-9]{1,3}(\.[0-9]{1,3}){3}' | tr '\n' ' ')"
        else
            echo "    WARNING: recreated but still stale -- check the host's /etc/resolv.conf"
            failed=$((failed+1))
        fi
    else
        echo "    FAILED to recreate"
        failed=$((failed+1))
    fi
done

echo
[ "$failed" -eq 0 ] && echo "Done; all recreated containers now use the host's current resolvers." \
                    || echo "Done with $failed problem(s) -- see above."
exit $(( failed > 0 ))
