# 💡 AOS(SS) Project – A.Y. 2024/2025

**Course:** Advanced Operating Systems (and System Security) [AOS(SS)]  
**Student:** Matteo Basili  
**Professor:** Francesco Quaglia  

---

## 📌 Project Objectives

The objective of this project is to design and implement a Linux Kernel Module (LKM) that provides a **block-device snapshot service** for file systems mounted on specific devices. The snapshot mechanism aims to transparently capture and log modifications to disk blocks, enabling the restoration of the file system to its original state prior to the mount operation.

Key goals of the project include:

- **Snapshot Activation and Deactivation APIs**  
  Implement two secure and root-only kernel-level API functions:  
  - `activate_snapshot(char * dev_name, char * passwd)`  
  - `deactivate_snapshot(char * dev_name, char * passwd)`  
  These functions control the activation and deactivation of the snapshot service for a specified block device. A custom password mechanism is employed to authenticate access.

- **Mount-time Snapshot Initialization**  
  When a device with an active snapshot configuration is mounted, the service automatically initializes and begins tracking changes.

- **Snapshot Data Management**  
  Create a dedicated subdirectory under `/snapshot`, named using the device name and a mount-time timestamp. This directory stores data required to reconstruct the original state of the file system.

- **Block-level Change Logging**  
  Monitor and log the original content of blocks modified by Virtual File System (VFS) operations. This allows recovery of the exact pre-mount file system state.

- **Asynchronous Processing with Deferred Work**  
  Minimize overhead on critical paths by offloading snapshot-related tasks using kernel deferred work mechanisms.

- **Snapshot Restoration**  
  Provide a mechanism to restore a previously recorded snapshot once the device has been unmounted. Restoration can be limited to device-level operations.

- **Compatibility with Loop Devices**  
  Support loop devices by treating the `dev_name` parameter as the file path associated with the loop device.

The final implementation must be testable using the minimal file system layout provided by the course instructors.  

---

## 📑 Documentation

📄 **Project Specification**: [block-device snapshot](https://francescoquaglia.github.io/TEACHING/AOS/CURRENT/PROJECTS/project-specification-2024-2025.html)
