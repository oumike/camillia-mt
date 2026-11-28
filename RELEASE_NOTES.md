### Changed
- CardputerZero: brightness and screen timeout now follow the launcher's Settings. Camillia picks them up each time it starts and hands the screen back at the launcher's brightness when it quits. The Lock Screen rows stay Camillia's; set the lock screen's brightness on the Web Config page. A system brightness below 10% shows as 10%.
- CardputerZero: message alerts and the splash melody now play through the system's sound at the launcher's volume. Notification Sound and Splash Melody still choose what plays.
- CardputerZero: the clock and timezone are the system's, kept by its own network time. Clock Format stays in Camillia.
- CardputerZero: Brightness, Screen Timeout, Volume, Time & Date, Time Zone, NTP Server, Power Saving and Battery Calibration are gone from Config and from the Web Config page.
- CardputerZero desktop window: it now has the touch boards' controls for the mouse (Send and Cancel buttons in the composer, an X on popups, tappable headers) and always shows the nav bar. The keyboard works as before. The window also draws at full brightness and never blanks, leaving both to the monitor and the desktop.

Pull request: #169
