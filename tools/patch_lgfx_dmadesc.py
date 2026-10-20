# Stops a failed SPI DMA descriptor allocation from crashing the device.
#
# LovyanGFX (and M5GFX, which vendors the same file) allocates its DMA
# descriptor array like this:
#
#   void Bus_SPI::_alloc_dmadesc(size_t len) {
#     if (_dmadesc) heap_caps_free(_dmadesc);          // frees FIRST
#     _dmadesc_size = len;
#     _dmadesc = heap_caps_malloc(sizeof(lldesc_t) * len, MALLOC_CAP_DMA);
#   }
#
# Three problems, all of which bite at once when internal DRAM runs out:
#
#   1. The old array is freed before the new one is obtained, so a failed
#      malloc leaves _dmadesc null rather than leaving the working array alone.
#   2. _dmadesc_size is updated whether or not the allocation succeeded, so it
#      then describes an array that does not exist.
#   3. _setup_dma_desc_links() dereferences _dmadesc with no null check, so the
#      next flush stores through null+4 and panics:
#
#        Guru Meditation Error: Core 1 panic'ed (StoreProhibited)
#        EXCVADDR: 0x00000004
#        ... Bus_SPI::_setup_dma_desc_links ... lvglFlush ...
#
#      And because of (2) the size test never asks for a new array again, so
#      every later flush of that size or smaller panics the same way. One
#      transient shortage becomes a permanent boot loop.
#
# The allocation involved is 12-24 bytes. It fails only when internal DMA-capable
# RAM is genuinely exhausted, which on the Cardputer happens while the SoftAP and
# the lite web-config page are both up. That headroom problem is tracked
# separately; this patch is only about not turning it into a crash.
#
# After patching, a failed allocation keeps the previous descriptor array and
# drops the frame instead. The panel shows a stale region for one refresh rather
# than the device rebooting mid-conversation.
#
# Scope: PlatformIO gives each environment its own libdeps copy, so this only
# touches the env that lists the script in extra_scripts.
#
# The source has moved between releases: Bus_SPI.cpp up to LovyanGFX 1.2.27,
# then Bus_SPI.inl (compiled as part of lgfx_v1_platforms.cpp); and from
# LovyanGFX 1.2.32 and M5GFX 0.2.32 the descriptor type is dma_desc_t instead
# of lldesc_t. The bug is the same in every one of them, so the patch takes
# each layout.
#
# Idempotent, and safe to run before libdeps exist: on a fresh checkout the first
# build may run this before the graphics library has been downloaded, in which
# case it warns and the patch lands on the next build. Once the library is there,
# anything short of a patched source fails the build -- no Bus_SPI source where
# one should be, or one that no longer matches -- rather than shipping it
# unpatched. The rename to Bus_SPI.inl once looked like a library not yet
# fetched, and a run of releases went out unpatched because of it.
Import("env")
import os

MARKER = "camillia-dmadesc-oom-patch"

OLD_ALLOC = """  void Bus_SPI::_alloc_dmadesc(size_t len)
  {
    if (_dmadesc) heap_caps_free(_dmadesc);
    _dmadesc_size = len;
    _dmadesc = (lldesc_t*)heap_caps_malloc(sizeof(lldesc_t) * len, MALLOC_CAP_DMA);
  }"""

NEW_ALLOC = """  void Bus_SPI::_alloc_dmadesc(size_t len)
  {
    // camillia-dmadesc-oom-patch: allocate before freeing, and only publish the
    // new array once it exists. The original freed first and assigned
    // _dmadesc_size unconditionally, so a failed malloc left a null _dmadesc
    // described by a non-zero size -- which _setup_dma_desc_links() then wrote
    // through. Keeping the previous array on failure means the bus stays usable
    // and the size still describes what _dmadesc actually points at.
    auto next = (lldesc_t*)heap_caps_malloc(sizeof(lldesc_t) * len, MALLOC_CAP_DMA);
    if (next == nullptr) { return; }
    if (_dmadesc) heap_caps_free(_dmadesc);
    _dmadesc = next;
    _dmadesc_size = len;
  }"""

OLD_SETUP = """    if (_dmadesc_size * SPI_MAX_DMA_LEN < len)
    {
      _alloc_dmadesc(len / SPI_MAX_DMA_LEN + 1);
    }
    lldesc_t *dmadesc = _dmadesc;"""

NEW_SETUP = """    if (_dmadesc_size * SPI_MAX_DMA_LEN < len)
    {
      _alloc_dmadesc(len / SPI_MAX_DMA_LEN + 1);
    }
    // camillia-dmadesc-oom-patch: the allocation above can fail under memory
    // pressure. Writing the descriptor chain anyway means storing through null,
    // or walking off the end of an array too short for this transfer. Bail out
    // instead: the caller starts the DMA from whatever descriptors were already
    // there, so one frame is stale and the device stays up.
    if (_dmadesc == nullptr || _dmadesc_size * SPI_MAX_DMA_LEN < len) { return; }
    lldesc_t *dmadesc = _dmadesc;"""

# The patterns above are written with lldesc_t; newer releases use dma_desc_t.
DESC_TYPES = ("lldesc_t", "dma_desc_t")

# Where the Bus_SPI source lives in a graphics library, by release.
SOURCES = (
    os.path.join("src", "lgfx", "v1", "platforms", "esp32", "Bus_SPI.cpp"),
    os.path.join("src", "lgfx", "v1", "platforms", "esp32", "Bus_SPI.inl"),
)

libdeps = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))

# LovyanGFX, or M5GFX vendoring it: recognised by its source tree rather than
# by folder name, so a renamed install cannot pass for one not yet fetched.
libs = []
if os.path.isdir(libdeps):
    libs = sorted(name for name in os.listdir(libdeps)
                  if os.path.isdir(os.path.join(libdeps, name, "src", "lgfx", "v1")))

drifted = []


def patch_file(lib, path):
    """Patches one Bus_SPI source; False if it does not define the allocator."""
    rel = os.path.relpath(path, libdeps)
    with open(path, encoding='utf-8') as f:
        src = f.read()
    if "Bus_SPI::_alloc_dmadesc(" not in src:
        return False
    marker_count = src.count(MARKER)
    if marker_count == 2:
        print("[patch_lgfx_dmadesc] already patched: %s" % lib)
        return True
    if marker_count != 0:
        print("[patch_lgfx_dmadesc] ERROR: partial patch in %s" % rel)
        drifted.append(rel)
        return True
    for desc in DESC_TYPES:
        old_alloc = OLD_ALLOC.replace("lldesc_t", desc)
        old_setup = OLD_SETUP.replace("lldesc_t", desc)
        if old_alloc in src and old_setup in src:
            src = (src.replace(old_alloc, NEW_ALLOC.replace("lldesc_t", desc), 1)
                      .replace(old_setup, NEW_SETUP.replace("lldesc_t", desc), 1))
            with open(path, 'w', encoding='utf-8') as f:
                f.write(src)
            print("[patch_lgfx_dmadesc] patched %s Bus_SPI DMA descriptor handling (%s)"
                  % (lib, os.path.basename(path)))
            return True
    print("[patch_lgfx_dmadesc] ERROR: pattern not found in %s - version drift?" % rel)
    drifted.append(rel)
    return True


for lib in libs:
    found = False
    for rel in SOURCES:
        path = os.path.join(libdeps, lib, rel)
        if os.path.isfile(path) and patch_file(lib, path):
            found = True
    if not found:
        print("[patch_lgfx_dmadesc] ERROR: no Bus_SPI.cpp or Bus_SPI.inl in %s"
              " defines _alloc_dmadesc - layout changed?" % lib)
        drifted.append(lib)

if drifted:
    raise RuntimeError(
        "[patch_lgfx_dmadesc] refusing to build with an unpatched Bus_SPI source: %s"
        % ", ".join(drifted)
    )

if not libs:
    print("[patch_lgfx_dmadesc] graphics library not fetched yet under %s" % libdeps)
    print("[patch_lgfx_dmadesc] NOT patched - run the build once more")
