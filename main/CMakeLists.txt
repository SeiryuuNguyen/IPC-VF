file(GLOB SRC_FILES
    "src/*.c"
)

idf_component_register(
    # 1. Liệt kê tất cả các file mã nguồn (.c) cần biên dịch
    SRCS 
        "ipc-vf.c" 
        ${SRC_FILES}
    # 2. Liệt kê các thư mục chứa file header (.h) để trình biên dịch có thể #include
    INCLUDE_DIRS 
        "." 
        "include"
    # 3. Khai báo các component/thư viện khác mà thư mục main này phụ thuộc vào
    REQUIRES 
        spi_nand_flash_fatfs app_update
)