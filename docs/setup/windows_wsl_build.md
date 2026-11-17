# Building Firefox on Windows using WSL

These steps were verified to work as of June 2024.

1. Install WSL (Windows Subsystem for Linux) on Windows by running Windows Powershell as Administrator and typing:

   ```
   wsl --install
   ```

2. Reboot Windows

3. Upon reboot, you should be asked to set up a user and password for Linux. If not, open Ubuntu from the Start menu.

4. Follow the [Building Firefox On Linux](linux_build.md) instructions.

5. Run the following commands to install dependencies needed to build and run Firefox:

   ```
   sudo apt update
   sudo apt install libgtk-3-0 libasound2 libx11-xcb-dev
   ```
