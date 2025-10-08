# 🧪 HOW TO TEST

This guide describes how to verify that the **snapshot restore mechanism** works correctly for a snapshot created on a **block device** (device-file) that hosts the **minimal file system**.

---

## ⚙️ 1. Prepare the minimal file system

First, load the minimal file system module and create the test image.

From the `SINGLEFILE-FS` directory, run:

```bash
make
sudo make load-FS-driver
make create-fs
```

This will:

- Compile and load the minimal file system kernel module  
- Create the test image file (`image`) used as the block device

## 🧩 2. Load the snapshot module

Follow the [main guide](https://github.com/MatteoBasili/aos-ss--progetto-2024_25/blob/main/README.md) to load the **snapshot module** (for simplicity, already with a password embedded) and start the **user-space control tool**.

Once the user-space program (`bdev_snap_app`) is running, you can interact with the kernel module via `ioctl()` commands.

## 📸 3. Activate the snapshot

Activate the snapshot for the test device-file by entering:

- **Device name** → the **absolute path** of the `image` file  
- **Password** → the same one loaded with the module

## 📂 4. Save the original image

Copy the `image` file into the test directory:

```bash
cp SINGLEFILE-FS/image original_image/
```

This copy will serve as the reference for comparison after the restore test.

## 🔗 5. Mount the minimal file system

Mount the file system on a loop device using:

```bash
sudo make mount-fs
```

*(Run this command from inside the `SINGLEFILE-FS` directory.)*

After mounting, check the kernel log for the **mount timestamp**, which will be useful for verifying the restore process later:

```bash
sudo dmesg | tail
```

## ✏️ 6. Modify the file inside the file system

Use the provided user-space program (from the file system module) to modify the file within the mounted minimal file system.
For example, run:

```bash
./user/user ./mount/the-file "This file has been modified!" 0
```

This overwrites the content of `the-file` with a new message.

## 📤 7. Unmount the file system

When done, unmount the loop device to flush all changes:

```bash
sudo umount ./mount
```

## 🧾 8. Verify that the image has changed

From this directory, run:

```bash
chmod +x ./run_test_file_restore.sh
./run_test_file_restore.sh
```

This script compares the **original image** and the **modified image** to confirm that they are different — indicating that the file system was successfully altered.

## 🔁 9. Perform the snapshot restore

Now, use the user-space tool (`bdev_snap_app`) to **restore** the snapshot:

- Provide the **absolute path** of the modified image (same as before)  
- Use the **same password** used when the snapshot was created

After confirming, the tool should restore the image to its original snapshot state.

## ✅ 10. Verify successful restore

Run the verification script again:

```bash
./run_test_file_restore.sh
```

If the restore succeeded, the script should report that the **original image** and the **current image** are **identical**.

---

## 🏁 Test Result

If all steps complete successfully and the final verification passes,
the snapshot and restore functionality of the module are working correctly.

