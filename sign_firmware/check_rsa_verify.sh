if [ "x$1" == "x" ]; then
        echo "Usage: $0 path_to_signed_binary_firmware_file"
        exit 1
fi

dd if=$1 of=extracted_firmware.sign bs=1 count=512
dd if=$1 of=extracted_firmware.bin bs=1 skip=512
openssl dgst -verify rsa_key.pub -keyform PEM -sha256 -signature extracted_firmware.sign extracted_firmware.bin
rm extracted_firmware.sign extracted_firmware.bin
