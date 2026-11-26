all: certs/ca-bundle.pem
	LC_ALL=C.UTF-8 idf.py set-target esp32c6
	LC_ALL=C.UTF-8 idf.py build
	base64 build/pxtapp.bin > build/pxtapp.b64
	base64 build/bootloader/bootloader.bin > build/bootloader.b64
	base64 build/partition_table/partition-table.bin > build/partition-table.b64

include certs.mk
