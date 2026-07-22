#!/bin/bash
set -e

function print_ok(){ echo -e "\033[92m[OK]\e[0m"; }
function print_fail(){ echo -e "\033[91m[FAIL]\e[0m"; }

echo "Start build process..."

BUILD_ENV_IMG="vivado:2025.1"
#BUILD_ENV_IMG="vivado-jenkins-agent:2025.1"
RP_ARM_IMG="rp-ubuntu-arm:latest"

# Leave blank so the FPGA project uses GITHUB.
# GIT_FPGA_CONFIG=""
# Using a local repository
GIT_FPGA_CONFIG="GIT_MODE=LOCAL GIT_LOCAL_PATH=/workspace/redpitaya-fpga"

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)

echo "Project root directory: ${PROJECT_ROOT}"

GIT_COMMIT_SHORT=$(git -C "${PROJECT_ROOT}" rev-parse --short HEAD 2>/dev/null || echo "unknown")

echo "Registering QEMU binfmt..."
docker run --privileged --rm tonistiigi/binfmt --install all
print_ok

echo "Building Docker images..."
docker build -f "${SCRIPT_DIR}/Dockerfile.rp_ubuntu" -t ${RP_ARM_IMG} "${SCRIPT_DIR}"
echo -n "Docker images built successfully. "
print_ok

echo "Running Makefile inside Red Pitaya OS container..."
docker run --rm \
    -v "${PROJECT_ROOT}:/workspace" \
    -w /workspace \
    ${RP_ARM_IMG} \
    bash -c "
        make -f Makefile REVISION=${GIT_COMMIT_SHORT} ENABLE_PRODUCTION_TEST=0 BUILD_NUMBER=1
    "

if [[ $? -eq 0 ]]; then
    echo -n "ARM build complete. "
    print_ok
else
    echo -n "ARM build failed. "
    print_fail
    exit 1
fi

echo "Running Makefile.x86..."
docker run --rm \
    --entrypoint /bin/bash \
    --network host \
    --hostname vivado-builder \
    -v "${PROJECT_ROOT}:/workspace:delegated" \
    -w /workspace \
    -e JENKINS_URL="" \
    -e JENKINS_SECRET="" \
    -e DEBIAN_FRONTEND=noninteractive \
    ${BUILD_ENV_IMG} \
    -c "
        echo 'Setting up environment...'

        export DEBIAN_FRONTEND=noninteractive

        if [ -f /opt/Xilinx/Vivado/2025.1/settings64.sh ]; then
            source /opt/Xilinx/Vivado/2025.1/settings64.sh
            echo 'Vivado sourced from /opt/Xilinx/Vivado/2025.1/'
        elif [ -f /opt/Xilinx/2025.1/Vivado/settings64.sh ]; then
            source /opt/Xilinx/2025.1/Vivado/settings64.sh
            echo 'Vivado sourced from /opt/Xilinx/2025.1/Vivado/'
        else
            echo 'ERROR: Vivado settings not found!'
            echo 'Searching for settings64.sh...'
            find /opt -name 'settings64.sh' 2>/dev/null | grep -i vivado || echo 'Not found'
            exit 1
        fi

        echo 'Vivado version:'
        vivado -version 2>/dev/null || echo 'WARNING: vivado command not found'

        # Start virtual framebuffer safely
        Xvfb :99 -screen 0 1024x768x24 &
        sleep 2 # Give Xvfb a moment to initialize
        export DISPLAY=:99

        # Environment overrides for legacy Java/Eclipse tools
        export GDK_BACKEND=x11
        export XILINX_DISPLAY=0
        export XSCT_HEADLESS=1
        export XSCT_NO_GTK=1
        export XILINX_DISPLAY=0
        export SWT_GTK3=1
        export JAVA_TOOL_OPTIONS='-Djava.awt.headless=true'
        export ARCH='arm'
        export CROSS_COMPILE='arm-linux-gnueabihf-'

#Assembling FPGA projects

       make -f Makefile.x86 fpga MODEL=Z10 ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z10_PRO_V2 ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z10_V2 ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z20_125_V2 ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z20 ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z20_125 ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z20_125_4CH ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z20_250_12 ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z20_250_12a ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z20_250_12_1_0 ${GIT_FPGA_CONFIG}
       make -f Makefile.x86 fpga MODEL=Z20_LL ${GIT_FPGA_CONFIG}

#Assembling Kernel

       make -f Makefile.x86 ${GIT_FPGA_CONFIG}
    "

echo "Packaging zip archive..."
docker run --rm \
    --entrypoint /bin/bash \
    -v "${PROJECT_ROOT}:/workspace" \
    -w /workspace \
    -e JENKINS_URL="" \
    -e JENKINS_SECRET="" \
    -e DEBIAN_FRONTEND=noninteractive \
    ${BUILD_ENV_IMG} \
    -c "
        make -f Makefile.x86 zip ${GIT_FPGA_CONFIG}
    "


echo -n "Build completed successfully! "
print_ok
