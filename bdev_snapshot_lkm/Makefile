# =======================================
# Makefile wrapper for bdev_snapshot LKM
# =======================================

SRC_DIR := ./src
USER_DIR := ./user
PASS_FILE := ./secret/the_snapshot_secret

all:
	@echo ">>> Building kernel module..."
	$(MAKE) -C $(SRC_DIR) all
	@echo ">>> Building user-space program..."
	$(MAKE) -C $(USER_DIR) all

clean:
	@echo ">>> Cleaning kernel module..."
	$(MAKE) -C $(SRC_DIR) clean
	@echo ">>> Cleaning user-space program..."
	$(MAKE) -C $(USER_DIR) clean
	@rm -f Module.symvers modules.order .Module.symvers.cmd .modules.order.cmd
	@rm -rf .tmp_versions

# Load the module without the password
load:
	sudo insmod $(SRC_DIR)/bdev_snapshot.ko

# Load the module with the password (from file ./secret/the_snapshot_secret)
load-with-pass:
	@if [ -f $(PASS_FILE) ]; then \
	  PASS=$$(tr -d '\n' < $(PASS_FILE)); \
	  sudo insmod $(SRC_DIR)/bdev_snapshot.ko snap_passwd="$$PASS"; \
	else \
	  echo "File $(PASS_FILE) non trovato"; exit 1; \
	fi

unload:
	sudo rmmod bdev_snapshot

