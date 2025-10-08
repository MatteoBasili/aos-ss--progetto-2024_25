# 💡 AOS(SS) Project – A.Y. 2024/2025

**Course:** Advanced Operating Systems (and System Security) [AOS(SS)]  
**Student:** Matteo Basili  
**Professor:** Francesco Quaglia  

---

## 📝 Project Overview

### 📌 Objectives

The objective of this project is to design and implement a Linux Kernel Module (LKM) that provides a **block-device snapshot service** for file systems mounted on specific devices. The snapshot mechanism aims to transparently capture and log modifications to disk blocks, enabling the restoration of the file system to its original state prior to the mount operation.

### 📄 Specification

[block-device snapshot](https://francescoquaglia.github.io/TEACHING/AOS/CURRENT/PROJECTS/project-specification-2024-2025.html)

---

## 🛠️ Implementation Overview

The implemented solution provides a complete snapshot service for **block devices**, with a focus on **efficiency, security, and testability**.

> ⚠️ **Note:** In this first version, the project provides **full snapshot support only for block devices mounting the minimal file system provided** (at [this link](https://francescoquaglia.github.io/TEACHING/AOS/CURRENT/PROJECTS/SINGLEFILE-FS.tar)) by the course instructor. **Other file systems are not yet supported**.  
> The **restore functionality** has been implemented and tested **exclusively for device files**. Restoring mounted file systems on **real devices is not currently supported**.

Key aspects of the implementation include:

- **Kernel Module Design**  
  - A character device `/dev/bdev_snapshot_ctrl` is exposed to perform all snapshot operations.  
  - Snapshot activation, deactivation, and restoration are implemented using **ioctl-based APIs**, accessible only to users with **root privileges**.  
  - A custom **password mechanism** is employed to authenticate access.  
  - The funcionality to set a new password for the snapshot service is also provided via ioctl.  
- **Snapshot Storage**  
  - Snapshots are stored in dedicated subdirectories under `/snapshot/`, named with the device identifier and mount timestamp.  
  - Only modified blocks are logged, allowing **incremental snapshots** without duplicating the entire device content.  
- **Deferred Work for Performance**  
  - Snapshot logging and bookkeeping are handled asynchronously via **kernel deferred work**, minimizing overhead on regular VFS operations.  
- **User-Space Control Tool**  
  - An interactive CLI (`bdev_snap_app`), supporting secure password input and preventing memory exposure of credentials, provides menu-driven control over snapshot operations.  
- **Testing with Minimal File System**  
  - The minimal file system layout and logic are included in `test/` for reproducible testing.  
  - Test utilities (`run_test_file_restore.sh`, `file_compare`) verify that **restored device files exactly match their original content**.  
- **Security and Robustness**  
  - Snapshot operations require a password, which is **securely cleared from memory** after use.  
  - Only threads with effective root privileges can interact with the module.  
  - Careful memory handling ensures no leakage of sensitive information.

---

## 📁 Project Structure (bdev_snapshot_lkm)

| Folder / File                    | Description                                                                 |
|---------------------------------|-----------------------------------------------------------------------------|
| `secret/`                       | Contains the snapshot password (`the_snapshot_secret`) used to initialize the kernel module securely.                                |
| `src/`                      | Source code of the **kernel module** (`bdev_snapshot.ko`), including all `.c` and `.h` files required to implement snapshot functionality, plus the kernel Makefile.                                                       |
| `test/`             | Contains test scripts and utilities to verify the snapshot and restore functionalities. It also includes the **minimal file system layout** used to test the module.                                              |
| `user/`                  | Source code and Makefile for the **user-space control tool** (`bdev_snap_app`) that interacts with the kernel module via `ioctl()`, providing an interactive menu for snapshot management.                                          |
| `Makefile`                       | Top-level Makefile wrapper that builds both the kernel module and the user-space tools, and provides commands to load/unload the module, optionally with the password.      |

---

## ⚙️ Setup and Execution

> ⚠️ **Note:** The project has been **developed and tested on Linux kernel version 6.8**. Proper functionality on other kernel versions **is not guaranteed**.

### 🔧 Prerequisites

Before building and running the project, ensure that the following requirements are met:

- **Linux distribution** with kernel **≥ 6.3** (for the minimal file system)  
- **Root privileges** are required to load kernel modules and perform snapshot operations

Install the essential development tools and kernel headers:

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

### 📖 Usage

#### 🧱 1. Build the project

From the `bdev_snapshot_lkm` directory, simply run:

```bash
make all
```
This command:

- Compiles the **kernel module** located in `./src`  
- Compiles the **user-space control tool** located in `./user`  
- Produces two main outputs:
  - `src/bdev_snapshot.ko` → kernel module
  - `user/bdev_snap_app` → user-space management utility

#### 🔌 2. Load the kernel module

You can load the module in **two ways**, depending on whether you want to specify a predefined password.

##### Option 1 – Load without password

```bash
make load
```

This inserts the module into the running kernel without any password protection.

##### Option 2 – Load with password

If you have stored a password in the file `./secret/the_snapshot_secret`, you can load the module as follows:

```bash
make load-with-pass
```

This will:

- Read the password from `./secret/the_snapshot_secret`  
- Pass it as a parameter (`snap_passwd`) to the module during insertion

#### 🧩 3. Verify successful module loading

Check the kernel log:

```bash
sudo dmesg | tail
```

#### 🖥️ 4. Start the user-space control application

Once the module is loaded, launch the control program:

```bash
sudo ./user/bdev_snap_app
```

This CLI interacts with the kernel module via `ioctl()` system calls, allowing you to:

- Activate or deactivate snapshots  
- Set or update the snapshot password  
- Restore a previously saved snapshot

#### 🧹 5. Unload the module and cleanup

When finished, unload the kernel module safely with:

```bash
make unload
```

If you wish to clean all build artifacts:

```bash
make clean
```

---

## 🧪 Testing

To test that the module is working correctly, follow this [guide](https://github.com/MatteoBasili/aos-ss--progetto-2024_25/blob/main/bdev_snapshot_lkm/test/README.md).

