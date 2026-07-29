FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    wget \
    curl \
    mc \
    git \
    unzip \
    zip \
    make \
    cmake \
    swig \
    gcc \
    g++ \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    gcc-mingw-w64-x86-64-posix \
    g++-mingw-w64-x86-64-posix \
    ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && rm -rf /tmp/*

RUN curl -Ls https://github.com/mamba-org/micromamba-releases/releases/download/2.8.1-0/micromamba-linux-64 -o /usr/local/bin/micromamba && \
    chmod +x /usr/local/bin/micromamba

# Set up environment variables for Micromamba paths
ENV MAMBA_ROOT_PREFIX=/opt/conda
ENV PATH=$MAMBA_ROOT_PREFIX/bin:$PATH

# Add this right after your ENV lines
SHELL ["/bin/bash", "-c"]

# Fix: Create root prefix and initialize the shell script using the correct -r flag
RUN mkdir -p /opt/conda && \
    micromamba shell init -s bash -r $MAMBA_ROOT_PREFIX

# Configure channels (Micromamba uses MAMBA_ROOT_PREFIX automatically)
RUN micromamba config append channels conda-forge && \
    micromamba config set channel_priority flexible

# 1. Create native Linux x86_64 environments
RUN for version in 3.9 3.10 3.11 3.12 3.13; do \
    env_name=py${version/./}; \
    echo "Creating environment: $env_name with Python $version"; \
    micromamba create -n "$env_name" python="$version" -y || exit 1; \
    done && \
    micromamba clean --all -y

COPY environment.yml /tmp/environment.yml

# 2. Install packages from environment.yml into native Linux x86_64 environments
RUN for version in 3.9 3.10 3.11 3.12 3.13; do \
    env_name=py${version/./}; \
    echo "Installing packages from environment.yml into $env_name"; \
    micromamba env update -n "$env_name" -f /tmp/environment.yml -y || exit 1; \
    done && \
    micromamba clean --all -y

# Create target directories for cross-compilation artifacts
RUN mkdir -p /opt/cross_artifacts/arm64 /opt/cross_artifacts/windows

# 3. Cross-platform download: Python for ARM64 (Linux Aarch64)
RUN for version in 3.9 3.10 3.11 3.12 3.13; do \
    env_name=py${version/./}; \
    echo "Extracting Python $version for ARM64..."; \
    CONDA_SUBDIR=linux-aarch64 micromamba create -p "/opt/cross_artifacts/arm64/$env_name" python="$version" --platform linux-aarch64 -y || exit 1; \
    done && \
    micromamba clean --all -y

# 4. Cross-platform download: Python for Windows (x86_64)
RUN for version in 3.9 3.10 3.11 3.12 3.13; do \
    env_name=py${version/./}; \
    echo "Extracting Python $version for Windows..."; \
    CONDA_SUBDIR=win-64 micromamba create -p "/opt/cross_artifacts/windows/$env_name" python="$version" --platform win-64 -y || exit 1; \
    done && \
    micromamba clean --all -y

# Add convenience script to activate environments
RUN echo '#!/bin/bash\n\
    if [ -z "$1" ]; then\n\
    echo "Usage: activate_env <env_name>"\n\
    echo "Available environments:"\n\
    micromamba env list\n\
    else\n\
    source /opt/conda/etc/profile.d/mamba.sh\n\
    micromamba activate "$1"\n\
    fi' > /usr/local/bin/activate_env && \
    chmod +x /usr/local/bin/activate_env

RUN mkdir -p /opt/python/linux/include \
    /opt/python/linux/lib \
    /opt/python/linux-arm64/include \
    /opt/python/linux-arm64/lib \
    /opt/python/windows/include \
    /opt/python/windows/lib

RUN for version in 3.9 3.10 3.11 3.12 3.13; do \
    env_name=py${version/./}; \
    src_inc="/opt/conda/envs/$env_name/include/python$version"; \
    src_lib="/opt/conda/envs/$env_name/lib"; \
    if [ -d "$src_inc" ]; then \
    ln -s "$src_inc" "/opt/python/linux/include/python$version"; \
    fi; \
    if [ -d "$src_lib" ]; then \
    ln -s "$src_lib" "/opt/python/linux/lib/python$version"; \
    fi; \
    done

RUN for version in 3.9 3.10 3.11 3.12 3.13; do \
    env_name=py${version/./}; \
    src_inc="/opt/cross_artifacts/arm64/$env_name/include/python$version"; \
    src_lib="/opt/cross_artifacts/arm64/$env_name/lib"; \
    if [ -d "$src_inc" ]; then \
    ln -s "$src_inc" "/opt/python/linux-arm64/include/python$version"; \
    fi; \
    if [ -d "$src_lib" ]; then \
    ln -s "$src_lib" "/opt/python/linux-arm64/lib/python$version"; \
    fi; \
    done

RUN for version in 3.9 3.10 3.11 3.12 3.13; do \
    env_name=py${version/./}; \
    src_dir="/opt/cross_artifacts/windows/$env_name"; \
    src_inc="$src_dir/include"; \
    src_lib="$src_dir/libs"; \
    \
    if [ -d "$src_inc" ]; then \
    ln -s "$src_inc" "/opt/python/windows/include/python$version"; \
    fi; \
    \
    if [ -d "$src_lib" ]; then \
    ln -s "$src_lib" "/opt/python/windows/lib/python$version"; \
    fi; \
    \
    if [ -d "$src_dir" ]; then \
    find "$src_dir" -maxdepth 1 -name "python3*.dll" -exec cp {} "/opt/python/windows/lib/python$version/" \;; \
    fi; \
    \
    compact=$(echo "$version" | tr -d '.'); \
    libdir="/opt/python/windows/lib/python$version"; \
    if [ -f "$libdir/python${compact}.lib" ]; then \
    cp "$libdir/python${compact}.lib" "$libdir/python${version}.lib"; \
    fi; \
    if [ -f "$libdir/python${compact}.dll" ]; then \
    cp "$libdir/python${compact}.dll" "$libdir/python${version}.dll"; \
    fi; \
    done

RUN git config --global --add safe.directory /workspace

WORKDIR /workspace
CMD ["/bin/bash"]