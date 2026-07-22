## 1. Building the Docker Container for Vivado

Before you can assemble the ecosystem, you must first build the Docker container for Vivado. This container provides the necessary environment for running Vivado tools.

> **Important Note:** The build process requires approximately **350 GB** of available disk space. Please ensure you have sufficient free space before proceeding.

### Step-by-Step Instructions:

1. **Navigate to the Docker directory**

   Open your terminal and change to the directory containing the Vivado Dockerfile:
   ```
   cd docker/vivado_2025.1/
   ```

2. **Verify the Dockerfile exists**

   Check that the file `Dockerfile.vivado` is present in the current directory:
   ```
   ls -la Dockerfile.vivado
   ```
   You should see the file listed in the output.

3. **Build the Docker container**

   Run the following command to build the container. The `-t` flag allows you to tag the image with a name for easy reference:
   ```
   docker build -t vivado:2025.1 -f Dockerfile.vivado .
   ```
   > **Note:** The build process will take significant time (potentially several hours) depending on your system's performance and network speed.

4. **Monitor the build progress**

   The terminal will display verbose output during the build. Watch for any error messages. Common issues include:
   - Insufficient disk space (ensure 450 GB is available)
   - Network interruptions (stable internet connection is required)
   - Permission errors (consider using `sudo` if necessary)

5. **Confirm successful build**

   Once the build completes, verify that the image was created successfully:
   ```
   docker images | grep vivado
   ```
   You should see `vivado` with the tag `2025.1` listed in the output.



---

## 2. Setting Up the FPGA Repository

The ecosystem requires the FPGA repository to function properly. You have two options for obtaining it: using a local path or letting the build script download it automatically from GitHub.

---

### Option A: Use a Local Repository (Recommended for Local Development)

If you already have the FPGA repository cloned on your system or prefer to manage it manually, use this option.

1. **Open the build script**

   Locate and open the `build.sh` file in your preferred text editor:
   ```
   nano build.sh
   ```
   or
   ```
   vim build.sh
   ```

2. **Set the `GIT_FPGA_CONFIG` variable for local mode**

   Find the `GIT_FPGA_CONFIG` line and set it as follows:
   ```
   GIT_FPGA_CONFIG="GIT_MODE=LOCAL GIT_LOCAL_PATH=/workspace/redpitaya-fpga"
   ```
   > **Important:** Replace `/workspace/redpitaya-fpga` with the actual absolute path to your local FPGA repository.

3. **Save and close the file**

   Save your changes and exit the text editor.

4. **Ensure the repository exists at the specified path**

   Verify that the FPGA repository is present at the location you specified:
   ```
   ls -la /workspace/redpitaya-fpga/
   ```
   You should see the repository files listed.

---

### Option B: Download from GitHub Automatically

To let the build script clone the FPGA repository directly from GitHub during the build process, simply leave the configuration variable blank.

1. **Open the build script**

   Locate and open the `build.sh` file:
   ```
   nano build.sh
   ```

2. **Leave the `GIT_FPGA_CONFIG` variable blank**

   Find the `GIT_FPGA_CONFIG` line and set it to an empty string:
   ```
   GIT_FPGA_CONFIG=""
   ```
   > **Note:** When left blank, the build script will automatically clone the FPGA project from GitHub.

3. **Save and close the file**

   Save your changes and exit the text editor.

4. **Run the build script**

   The repository will be automatically downloaded from GitHub during the build process:
   ```
   ./build.sh
   ```

---

### Summary Table

| Option | Configuration | When to Use |
|--------|---------------|-------------|
| **A: Local Repository** | `GIT_FPGA_CONFIG="GIT_MODE=LOCAL GIT_LOCAL_PATH=/workspace/redpitaya-fpga"` | You already have the repository locally or prefer manual control |
| **B: GitHub Auto-Download** | `GIT_FPGA_CONFIG=""` | You want the script to handle cloning automatically |


---

## 3. Running the Build Script

Once all prerequisites are configured, you can launch the build process. The script you use depends on your operating system.

---

### Option A: Ubuntu / Linux

Use the `build.sh` script for Ubuntu or any Linux-based system.

1. **Navigate to the ecosystem directory**

   Open a terminal and change to the ecosystem folder inside the Docker directory:
   ```
   cd docker/ecosystem/
   ```

2. **Ensure the script has execution permissions**

   If you encounter permission issues, make the script executable:
   ```
   chmod +x build.sh
   ```

3. **Run the build script**

   Execute the script with the following command:
   ```
   ./build.sh
   ```
   > **Note:** The build process will take significant time (potentially several hours) depending on your system's performance. The script will display verbose output showing the progress of each step.

4. **Monitor the build progress**

   Watch the terminal output for:
   - Success messages indicating completed stages
   - Error messages (if any) that may require your attention
   - Progress indicators showing current operations

---

### Option B: Windows

Use the `build.ps1` PowerShell script for Windows systems.

1. **Open PowerShell**

   - Press `Win + X` and select **Windows PowerShell** or **Terminal**
   - Alternatively, search for "PowerShell" in the Start menu

2. **Navigate to the ecosystem directory**

   Change to the ecosystem folder inside the Docker directory:
   ```
   cd docker\ecosystem\
   ```
   > **Note:** Replace the path with the actual location of your project.

3. **Run the build script**

   Execute the script with the following command:
   ```
   .\build.ps1
   ```
   > **Note:** The build process will take significant time. The script will display detailed progress information in the console.

4. **Monitor the build progress**

   Watch the console output for:
   - Success messages indicating completed stages
   - Error messages (if any) that may require your attention
   - Progress indicators showing current operations

---

### PowerShell Execution Policy (If Needed)

In most cases, you can run `.\build.ps1` without any additional configuration. However, if you encounter an error like:

> *"File cannot be loaded because running scripts is disabled on this system"*

You have two options to resolve this:

**Option 1:** Allow script execution for the current session only (recommended):
```
Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
```
Then run the script again:
```
.\build.ps1
```
> **Note:** This change applies only to the current PowerShell session and does not affect system-wide settings.

**Option 2:** Run PowerShell as Administrator and change the policy permanently (use with caution):
```
Set-ExecutionPolicy RemoteSigned
```
> **Warning:** Changing the execution policy permanently may have security implications. Only do this if you understand the risks.

---

### Troubleshooting Common Issues

| Issue | Solution |
|-------|----------|
| **Permission denied (Linux)** | Run `chmod +x build.sh` to make the script executable |
| **Script execution disabled (Windows)** | Use `Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process` to enable scripts for the current session only |
| **Script not found** | Ensure you are in the correct directory: `docker/ecosystem/` |
| **Build fails with errors** | Check the error message in the output; verify that previous steps (Docker, FPGA repository) were completed successfully |
| **Insufficient disk space** | Ensure you have enough free space (the build process requires additional space beyond the initial 350 GB for Docker) |

