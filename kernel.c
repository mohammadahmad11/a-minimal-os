#define VGA_ADDRESS 0xB8000
#define VGA_ROW_MAX 25
#define VGA_COL_MAX 80

int cursor_row = 0;
int cursor_col = 0;

void vga_print_char(char c) {
    char *vga = (char *)VGA_ADDRESS;

    if (c == '\n') {
        cursor_row++;
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            int index = (cursor_row * VGA_COL_MAX + cursor_col) * 2;
            vga[index] = ' ';     // Clear character
            vga[index + 1] = 0x07; // Attribute
        }
    } else {
        int index = (cursor_row * VGA_COL_MAX + cursor_col) * 2;
        vga[index] = c;
        vga[index + 1] = 0x07; // Light grey on black
        cursor_col++;
        if (cursor_col >= VGA_COL_MAX) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    // Basic scroll handling if you hit the bottom of the screen can go here
    if (cursor_row >= VGA_ROW_MAX) {
        cursor_row = VGA_ROW_MAX - 1;
        // (Optional: write a fast scroll-up function later)
    }
}

void vga_print_string(char *str) {
    while (*str) {
        vga_print_char(*str++);
    }
}

void vga_clear_screen() {
    char *vga = (char *)VGA_ADDRESS;
    for (int i = 0; i < VGA_COL_MAX * VGA_ROW_MAX * 2; i += 2) {
        vga[i] = '0';
        vga[i] = ' ';      // Space
        vga[i + 1] = 0x07; // Attribute
    }
    cursor_row = 0;
    cursor_col = 0;
}

// Custom fixed-width types (x86 32-bit)
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void insw(uint16_t port, void *addr, uint32_t word_count) {
    __asm__ volatile ("rep insw" : "+D"(addr), "+c"(word_count) : "d"(port) : "memory");
}

// Basic US QWERTY scancode lookup table (Set 1)
static char scancode_ascii[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ' // Spacebar is around index 57
};

// Polls the keyboard until a valid printable/usable key press is detected
char get_keyboard_char() {
    while (1) {
        // Check if output buffer is full (bit 0 of port 0x64 means data is ready)
        if (inb(0x64) & 0x01) {
            uint8_t scancode = inb(0x60);

            // Ignore key releases (high bit set means released)
            if (scancode & 0x80) {
                continue;
            }

            // Translate scancode to ASCII if within bounds
            if (scancode < 128) {
                char ascii = scancode_ascii[scancode];
                if (ascii != 0) {
                    return ascii;
                }
            }
        }
    }
}

void shell_read_line(char *buffer, int max_size) {
    int current_len = 0;
    buffer[0] = '\0';

    while (1) {
        char c = get_keyboard_char();

        if (c == '\n') { // Enter key pressed
            vga_print_char('\n');
            buffer[current_len] = '\0';
            break;
        } 
        else if (c == '\b') { // Backspace
            if (current_len > 0) {
                current_len--;
                buffer[current_len] = '\0';
                vga_print_char('\b'); // Echo backspace on screen
            }
        } 
        else { // Standard printable character
            if (current_len < max_size - 1) {
                buffer[current_len++] = c;
                buffer[current_len] = '\0';
                vga_print_char(c); // Echo character to screen
            }
        }
    }
}

// Read a 512-byte sector from disk into a memory buffer
void ata_read_sector(uint32_t lba, uint8_t *target_buffer) {
    lba += 63;
    
    // 1. Wait until the disk is not busy
    while (inb(0x1F7) & 0x80);

    // 2. Send drive and LBA bits 24-27 (Master drive = 0xE0)
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));

    // 3. Send sector count (1 sector)
    outb(0x1F2, 1);

    // 4. Send LBA bytes (Low, Mid, High)
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));

    // 5. Send the Read Sectors command (0x20)
    outb(0x1F7, 0x20);

    // 5.5. 400ns delay (read alternate status 4 times)
    for (int i = 0; i < 4; i++) {
        inb(0x3F6);
    }

    // 6. Wait for the disk to be ready and data request (DRQ) to be set, or check for errors
    while (1) {
        uint8_t status = inb(0x1F7);
        
        // Check if an error occurred (ERR bit 0 or DF bit 5)
        if (status & 0x01 || status & 0x20) {
            // Handle error here (e.g., infinite loop, panic, or return error code)
            return; 
        }
        
        // Check if ready and data request is set
        if (!(status & 0x80) && (status & 0x08)) {
            break; // Ready!
        }
    }

    // 7. Read 256 words (512 bytes) from data port into memory
    insw(0x1F0, target_buffer, 256);
}

// Write a 512-byte sector from a memory buffer to disk
void ata_write_sector(uint32_t lba, uint8_t *source_buffer) {
    lba += 63;
    
    // 1. Wait until the disk is not busy
    while (inb(0x1F7) & 0x80);

    // 2. Send drive and LBA bits 24-27 (Master drive = 0xE0)
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));

    // 3. Send sector count (1 sector)
    outb(0x1F2, 1);

    // 4. Send LBA bytes (Low, Mid, High)
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));

    // 5. Send the Write Sectors command (0x30)
    outb(0x1F7, 0x30);

    // 5.5. 400ns delay (read alternate status 4 times)
    for (int i = 0; i < 4; i++) {
        inb(0x3F6);
    }

    // 6. Wait for DRQ to be set, or check for errors
    while (1) {
        uint8_t status = inb(0x1F7);
        
        // Check if an error occurred (ERR bit 0 or DF bit 5)
        if (status & 0x01 || status & 0x20) {
            return; // Error handling/exit
        }
        
        if (!(status & 0x80) && (status & 0x08)) {
            break; // Ready to receive data!
        }
    }

    // 7. Write 256 words (512 bytes total) into the data port
    uint16_t *ptr = (uint16_t *)source_buffer;
    for (int i = 0; i < 256; i++) {
        outw(0x1F0, ptr[i]);
    }

    // 8. Send Cache Flush command (0xE7) to ensure data is written to disk
    outb(0x1F7, 0xE7);
    while (inb(0x1F7) & 0x80);
}

// --------------------------------------------------------------------------------

void memcpy_b(uint8_t *src, uint8_t *dst, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        *(dst + i) = *(src + i);
    }
}

uint8_t memcmp_b(uint8_t *op1, uint8_t *op2, uint32_t size) {
    uint32_t i = 0;;
    while (i < size && *(op1 + i) == *(op2 + i)) {
        i++;
    }
    if (i == size) {
        return 1;
    } else {
        return 0;
    }
}

int string_compare(char *s1, char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

uint16_t read16bit_from_uint8_ptr(const uint8_t *ptr) {
    uint16_t out_val;
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        out_val = (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        out_val = ((uint16_t)ptr[0] << 8) | (uint16_t)ptr[1];
    #endif
    return out_val;
}

void write16bit_to_uint8_ptr(uint8_t* ptr, uint16_t value) {
    #if  __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        ptr[0] = (uint8_t)(value & 0xFF);
        ptr[1] = (uint8_t)(value >> 8);

    #else
        ptr[0] = (uint8_t)(value >> 8);
        ptr[1] = (uint8_t)(value & 0xFF);
    #endif
}

// --------------------------------------------------------------------------------

uint16_t locate_file_meta(uint8_t *path, uint8_t *sector_1) {
    uint16_t ret_val = 0xffff;
    uint16_t last_look = 0;
    char main_str[8] = {'m','a','i','n',0,0,0,0};
    if (memcmp_b(main_str, path, 8)) {
        if (*(path + 8) == 0) {
            ret_val = 0;
        } else {
            path += 8;
            uint8_t found;
            while (*(path) && *(sector_1 + last_look) == 1) {
                last_look += 10;
                found = 0;
                uint16_t dir_rec;
                while (read16bit_from_uint8_ptr(sector_1 + last_look) != 0 && !found) {
                    dir_rec = read16bit_from_uint8_ptr(sector_1 + last_look);
                    last_look += 2;
                    found = memcmp_b(path,sector_1 + dir_rec + 1,8);
                }
                if (found) {
                    path += 8;
                    last_look = dir_rec;
                    if (*path == 0) {
                        ret_val = dir_rec;
                    }
                } else {
                break; 
                }
            }
        }
    }
    return ret_val;
}

uint8_t locate_end_path_off(uint8_t *path) {
    uint8_t off = 0;
    while (*(path + off)) {
        off += 8;
    }
    return off;
}

uint16_t locate_end_all_meta(int8_t *sector_1) { // locates the null flag
    uint16_t end_off = 0;
    while (*(sector_1 + end_off) != 0) {
        if (*(sector_1 + end_off) == 1) {
            end_off += 10;
            while (read16bit_from_uint8_ptr(sector_1 + end_off) != 0) {
                end_off += 2;
            }
            end_off += 2;
        } else {
            end_off += 11;
        }
    }
    return end_off;
}

void shift_left(uint8_t *sector_1, uint16_t start, uint16_t degree) {
    uint16_t end = locate_end_all_meta(sector_1);
    while (start < end) {
        *(sector_1 + start - degree) = *(sector_1 + start);
        start++;
    }
    *(sector_1 + end - degree) = 0;
}

void shift_right(uint8_t *sector_1, uint16_t start, uint16_t degree) {
    uint16_t end_rem = locate_end_all_meta(sector_1);
    uint16_t end = end_rem - 1;
    while (end >= start) {
        *(sector_1 + end + degree) = *(sector_1 + end);
        end--;
    }
    *(sector_1 + end_rem + degree) = 0;
}

void if_gt_than_add(uint8_t *sector_1, uint16_t than, uint16_t add) {
    uint16_t end_off = 0;
    while (*(sector_1 + end_off) != 0) {
        if (*(sector_1 + end_off) == 1) {
            end_off += 10;
            while (read16bit_from_uint8_ptr(sector_1 + end_off) != 0) {
                if (read16bit_from_uint8_ptr(sector_1 + end_off) > than) {
                    uint16_t res = read16bit_from_uint8_ptr(sector_1 + end_off) + add;
                    write16bit_to_uint8_ptr(sector_1 + end_off, res);
                }
                end_off += 2;
            }
            end_off += 2;
        } else {
            end_off += 11;
        }
    }
}



void back_working_directory(uint8_t *path) {
    if (locate_end_path_off(path) != 8) {
        *(path + locate_end_path_off(path) - 8) = 0;
    } else {
        vga_print_string("cant exit main\n");
    }
}

void change_working_directory(uint8_t *sector_1, uint8_t *path) {
    if (locate_file_meta(path,sector_1) == 0xffff) {
        *(path + locate_end_path_off(path) - 8) = 0;
        vga_print_string("doesnt exist\n");
    } else if (*(sector_1 + locate_file_meta(path,sector_1)) != 1) {
        vga_print_string("not a dir\n");
        *(path + locate_end_path_off(path) - 8) = 0;
    } else if (locate_end_path_off(path) == 80) {
        *(path + locate_end_path_off(path) - 8) = 0;
        vga_print_string("cant go further deep in directories\n");
    }
}

void read_file(uint8_t *path, uint8_t *out, uint8_t *sector_1) {
    uint16_t meta = locate_file_meta(path, sector_1);
    if (meta != 0xffff) {
        uint8_t sec = *(sector_1 + meta + 10);
        ata_read_sector(sec,out);
    }
}

void write_file(uint8_t *path, uint8_t *in, uint8_t *sector_1) {
    uint16_t meta = locate_file_meta(path, sector_1);
    if (meta != 0xffff) {
        uint8_t sec = *(sector_1 + meta + 10);
        ata_write_sector(sec, in);
    }
}

void make_directory(uint8_t *sector_1, uint8_t *path) {
    uint8_t path_end = locate_end_path_off(path);   
    uint8_t a = *(path + path_end - 8);
    *(path + path_end - 8) = 0;
    uint16_t wd = locate_file_meta(path, sector_1);
    *(path + path_end - 8) = a;
    uint16_t nd = locate_file_meta(path, sector_1);
    if (*(sector_1 + wd) != 1) {
        wd = 0xffff;
    }
    if (wd != 0xffff && nd == 0xffff) {
        uint16_t meta_end = locate_end_all_meta(sector_1);
        *(sector_1 + meta_end) = 1;
        memcpy_b(path + path_end - 8,sector_1 + meta_end + 1,8);
        *(sector_1 + meta_end + 9) = 0;
        *(sector_1 + meta_end + 10) = 0;
        *(sector_1 + meta_end + 11) = 0;
        *(sector_1 + meta_end + 12) = 0;

        wd += 10;
        while (read16bit_from_uint8_ptr(sector_1 + wd) != 0) {
            wd += 2;
        }
        write16bit_to_uint8_ptr(sector_1 + wd, meta_end);
        shift_right(sector_1, wd, 2);
        *(sector_1 + wd + 2) = 0;
        if_gt_than_add(sector_1, wd, 2);
    }
}

void make_file(uint8_t *sector_1, uint8_t *path) {
    uint8_t path_end = locate_end_path_off(path);
    uint8_t a = *(path + path_end - 8);
    *(path + path_end - 8) = 0;
    uint16_t wd = locate_file_meta(path, sector_1);
    *(path + path_end - 8) = a;
    uint16_t nd = locate_file_meta(path, sector_1);
    if (*(sector_1 + wd) != 1) {
        wd = 0xffff;
    }
    if (wd != 0xffff && nd == 0xffff) {
        uint16_t meta_end = locate_end_all_meta(sector_1);
        *(sector_1 + meta_end) = 2;
        memcpy_b(path + path_end - 8,sector_1 + meta_end + 1,8);
        *(sector_1 + meta_end + 9) = 0;
        if (sector_1[510]) {
            uint8_t *ptr = sector_1 + 510;
            while (*(ptr)) {
                ptr--;
            }
            *(sector_1 + meta_end + 10) = *(ptr + 1);
            *(ptr + 1) = 0;
        } else {
            *(sector_1 + meta_end + 10) = sector_1[511];
            sector_1[511]++;
        }
        *(sector_1 + meta_end + 11) = 0;
        
        wd += 10;
        while (read16bit_from_uint8_ptr(sector_1 + wd) != 0) {
            wd += 2;
        }
        write16bit_to_uint8_ptr(sector_1 + wd, meta_end);
        shift_right(sector_1, wd + 2, 2);
        *(sector_1 + wd + 2) = 0;
        if_gt_than_add(sector_1, wd, 2);
    } else {
        // output failed
    }
}

void remove_file(uint8_t *sector_1, uint8_t *path) {
    uint16_t nd = locate_file_meta(path, sector_1);
    if (nd != 0xffff) {
        uint8_t path_end = locate_end_path_off(path);
        uint8_t a = *(path + path_end - 8);
        *(path + path_end - 8) = 0;
        uint16_t wd = locate_file_meta(path, sector_1);
        *(path + path_end - 8) = a;

        wd += 10;
        while (read16bit_from_uint8_ptr(sector_1 + wd) != nd) {
            wd += 2;
        }
        uint8_t f_sec = *(sector_1 + nd + 10);
        shift_left(sector_1, wd + 2, 2);
        if_gt_than_add(sector_1, wd, -2);
        shift_left(sector_1,nd-2+11,11);
        if_gt_than_add(sector_1,nd-2,-11);

        if (f_sec == sector_1[511] - 1) {
            sector_1[511]--;
        } else {
            uint8_t *ptr = sector_1 + 510;
            while (*ptr) {
                ptr--;
            }
            *ptr = f_sec;
            *(ptr - 1) = 0;
        }
    }
}

void remove_file_off(uint8_t *sector_1, uint16_t wd, uint16_t nd) {
    wd += 10;
    while (read16bit_from_uint8_ptr(sector_1 + wd) != nd) {
        wd += 2;
    }
    uint8_t f_sec = *(sector_1 + nd + 10);
    shift_left(sector_1, wd + 2, 2);
    if_gt_than_add(sector_1, wd, -2);
    shift_left(sector_1,nd-2+11,11);
    if_gt_than_add(sector_1,nd-2,-11);

    if (f_sec == sector_1[511] - 1) {
        sector_1[511]--;
    } else {
        uint8_t *ptr = sector_1 + 510;
        while (*ptr) {
            ptr--;
        }
        *ptr = f_sec;
        *(ptr - 1) = 0;
    }
}

void remove_directory_off(uint8_t *sector_1, uint16_t wd, uint16_t nd) {
    while(read16bit_from_uint8_ptr(sector_1 + nd + 10) != 0) {
        if (*(sector_1 + read16bit_from_uint8_ptr(sector_1 + nd + 10)) == 1) {
            remove_directory_off(sector_1, nd, read16bit_from_uint8_ptr(sector_1 + nd + 10));
        } else {
            remove_file_off(sector_1, nd, read16bit_from_uint8_ptr(sector_1 + nd + 10));
        }
    }
    wd += 10;
    while (read16bit_from_uint8_ptr(sector_1 + wd) != nd) {
        wd += 2;
    }
    shift_left(sector_1, wd + 2, 2);
    if_gt_than_add(sector_1, wd, -2);
    shift_left(sector_1,nd-2+12,12);
    if_gt_than_add(sector_1,nd-2,-12);
}

void remove(uint8_t *sector_1, uint8_t *path) {
    uint16_t meta = locate_file_meta(path, sector_1);
    if (meta != 0xffff) {
        if (*(sector_1 + meta) == 1) {
            uint16_t nd = locate_file_meta(path, sector_1);
            if (nd != 0xffff) {
                uint8_t path_end = locate_end_path_off(path);
                uint8_t a = *(path + path_end - 8);
                *(path + path_end - 8) = 0;
                uint16_t wd = locate_file_meta(path, sector_1);
                *(path + path_end - 8) = a;
                remove_directory_off(sector_1, wd, nd);
            }
        } else {
            remove_file(sector_1, path);
        }
    }
}

void exec_process(uint8_t *sector_1, uint8_t* path, uint32_t *exec_pos) {
    uint16_t meta = locate_file_meta(path, sector_1);
    if (meta != 0xffff) {
        uint8_t sec = *(sector_1 + meta + 10);
        *exec_pos += 0x100000;
        uint8_t* target = (uint8_t*) *exec_pos;
        ata_read_sector(sec,target);
        void (*prog)(void) = (void (*)(void)) *exec_pos;
        prog();
        *exec_pos -= 0x100000;
    }
}

void take_file_from_comm(char* command, uint8_t *path) {
    char *remem = command;
    command += 3;
    while (*command == ' ') {
        command++;
    }
    if (*command) {
        uint8_t size = 0;
        uint8_t path_end = locate_end_path_off(path);
        while (*command && *command != ' ' && size < 8) {
            command++;
            size++;
        }
        if (!*command || *command == ' ') {
            *command = 0;
            for (int i = 0; i < size; i++) {
                *(path + path_end + i) = *(command - size + i);
            }
            for (int i = 0; i < 8 - size + 1; i++) {
                *(path + path_end + size + i) = 0;
            }
        } else {
            vga_print_string("file / dir name is maximum 8 characters");
        }
    }
}

void ls() {

}


struct kernel_api {
    uint8_t sector_1[512];
    uint32_t exec_pos;
    void (*md)(uint8_t *sector_1, uint8_t *path);
    void (*mf)(uint8_t *sector_1, uint8_t *path);
    void (*rm)(uint8_t *sector_1, uint8_t *path);
    void (*ex)(uint8_t *sector_1, uint8_t *path, uint32_t *exec_pos);
    void (*rf)(uint8_t *path, uint8_t *out, uint8_t *sector_1);
    void (*wf)(uint8_t *path, uint8_t *in, uint8_t *sector_1);
    uint16_t (*lf)(uint8_t *path, uint8_t *sector_1);
    void (*in)(char *buffer, int max_size);
    void (*ou)(char *str);
};

void kmain(void) {
    struct kernel_api *api = (struct kernel_api *)0x1000;
    api->exec_pos = 0x100000;
    api->md = &make_directory;
    api->mf = &make_file;
    api->rm = &remove;
    api->ex = &exec_process;
    api->rf = &read_file;
    api->wf = &write_file;
    api->lf = &locate_file_meta;
    api->in = &shell_read_line;
    api->ou = &vga_print_string;
    
    char curr_path[81] = {'m','a','i','n',0,0,0,0,  0}; // length is 10 (inclusive of endpoint)
    curr_path[80] = 0;
    ata_read_sector(1,api->sector_1);
    if (*api->sector_1 != 1) {
        
        api->sector_1[0] = 1; // dir flag = 1, file flag = 2

        api->sector_1[1] = 'm'; // name len = 8
        api->sector_1[2] = 'a';
        api->sector_1[3] = 'i';
        api->sector_1[4] = 'n';
        api->sector_1[5] = 0;
        api->sector_1[6] = 0;
        api->sector_1[7] = 0;
        api->sector_1[8] = 0;
        
        api->sector_1[9] = 0; // null terminating string
        
        api->sector_1[10] = 0; // null terminating 16 bit offset addr for branched nodes (not here for files)
        api->sector_1[11] = 0;

        api->sector_1[12] = 0; // null flag (nothing ahead)

        api->sector_1[510] = 0; // null terminated stack
        api->sector_1[511] = 2; // sector of yet to be file if stack empty

        // btw max number of files is 254
    }



    char command_buffer[128];

    vga_clear_screen();

    while (1) {
        uint8_t* curr_ptr = curr_path;
        while (*curr_ptr) {
            uint8_t a = *(curr_ptr + 8);
            *(curr_ptr + 8) = 0;
            vga_print_string(curr_ptr);
            vga_print_char('\\');
            *(curr_ptr + 8) = a;
            curr_ptr += 8;
        }
        vga_print_string("> ");

        // Read user input into the buffer
        shell_read_line(command_buffer, sizeof(command_buffer));

        // Evaluate commands
        if (string_compare(command_buffer, "clear") == 0) {
            vga_clear_screen();

        } else if (string_compare(command_buffer, "ls") == 0) {
            uint16_t wd = locate_file_meta(curr_path, api->sector_1);
            wd += 10;
            if (read16bit_from_uint8_ptr(api->sector_1 + wd) != 0) {
                while (read16bit_from_uint8_ptr(api->sector_1 + wd) != 0) {
                    if (*(api->sector_1 + read16bit_from_uint8_ptr(api->sector_1 + wd)) == 1) {
                        vga_print_string("dir   ");
                    } else {
                        vga_print_string("fil   ");
                    }
                    vga_print_string(api->sector_1 + read16bit_from_uint8_ptr(api->sector_1 + wd) + 1);
                    vga_print_char('\n');
                    wd += 2;
                }
            } else {
                vga_print_string("nothing\n");
            }
        } else if (memcmp_b(command_buffer, "cd ", 3)) {
            take_file_from_comm(command_buffer, curr_path);
            change_working_directory(api->sector_1, curr_path);

        } else if (string_compare(command_buffer, "cb") == 0) {
            back_working_directory(curr_path);

        } else if (memcmp_b(command_buffer, "md ", 3)) {
            take_file_from_comm(command_buffer, curr_path);
            make_directory(api->sector_1, curr_path);
            *(curr_path + locate_end_path_off(curr_path) - 8) = 0;

        } else if (memcmp_b(command_buffer, "mf ", 3)) {
            take_file_from_comm(command_buffer, curr_path);
            make_file(api->sector_1, curr_path);
            *(curr_path + locate_end_path_off(curr_path) - 8) = 0;

        } else if (memcmp_b(command_buffer, "rm ", 3)) {
            take_file_from_comm(command_buffer, curr_path);
            remove(api->sector_1, curr_path);
            *(curr_path + locate_end_path_off(curr_path) - 8) = 0;

        } else if (memcmp_b(command_buffer, "ex ", 3)) {
            take_file_from_comm(command_buffer, curr_path);
            exec_process(api->sector_1, curr_path, &(api->exec_pos));
            *(curr_path + locate_end_path_off(curr_path) - 8) = 0;

        } else if (command_buffer[0] == '\0') {
            // Do nothing if user just hit enter
        } else {
            vga_print_string("Unknown command. Type 'help'.\n");
        }
    }




    // gonna be ata writings sector_1 every loop









}