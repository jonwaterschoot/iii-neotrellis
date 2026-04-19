#include "hardware/watchdog.h"
#include "pico/bootrom.h"

#include "fs.h"
#include "repl.h"
#include "serial.h"
#include "vm.h"

//------------------------------------------------
//--- constants and state

//--- input buffers
static lfs_file_t upload_file;
static char temp_name_buffer[256];
static uint32_t script_rx_pos = 0;
static char filename[256] = "init.lua";

// FIXME: made the buffer huge for import/export flash via prints
// could more sensibly do this at a lower level and save RAM
// FIXED?? this isn't necessary, linebuffer is for repl only, file stuff is managed with another buffer that is malloc'd. idk what a sensible linebuffer size is though, so 512 seems fine
// FIXME: choose linebuffer size sensibly!
//#define LINE_BUFFER_SIZE 16383
#define LINE_BUFFER_SIZE 512
static char line_buf[LINE_BUFFER_SIZE + 1];
static uint32_t line_buf_pos = 0;

//--- other state
bool in_rx_script = false;

enum {
  REPL_CMD_CLEAN = 'C',
  REPL_CMD_INIT = 'I',
  REPL_CMD_RESET = 'R',
  REPL_CMD_START_DATA = 'S',
  REPL_CMD_END_FILENAME = 'F',
  REPL_CMD_END_WRITE = 'W',
  REPL_CMD_PRINT = 'P',
  REPL_CMD_GET_FILENAME = 'G',
  REPL_CMD_BOOTLOAD = 'B'
};

//------------------------------------------------
//---- static helper functions

static void reset_script_rx() {
  script_rx_pos = 0;
  in_rx_script = false;
}

//------------------------------------------------
//--- external functions

void repl_init() {
  // nothing to do
}

bool handle_command(char cmd) {
  switch (cmd) {
  case REPL_CMD_CLEAN:
    serial("-- re-init with no script\r\n");
    vm_deinit();
    vm_init(false);
    break;

  case REPL_CMD_INIT:
    serial("-- re-init\r\n");
    vm_deinit();
    vm_init(true);
    break;

  case REPL_CMD_BOOTLOAD:
    serial("-- launching bootloader...\r\n");
    rom_reset_usb_boot(0, 0);
    break;

  case REPL_CMD_RESET:
    serial("-- reset device...\r\n");
    watchdog_reboot(0, 0, 1000);
    break;

  case REPL_CMD_START_DATA:
    if (fs_file_open(&upload_file, ".uploading.tmp", LFS_O_CREAT | LFS_O_WRONLY) == 0) {
      in_rx_script = true;
      script_rx_pos = 0;
      serial("-- receiving data\r\n");
    } else {
      serial("-- upload file open fail!\r\n");
    }
    break;

  case REPL_CMD_END_FILENAME:
    if(in_rx_script) {
      if (script_rx_pos < sizeof(temp_name_buffer)) {
        temp_name_buffer[script_rx_pos] = '\0';
      } else {
        temp_name_buffer[sizeof(temp_name_buffer)-1] = '\0';
      }

      fs_file_close(&upload_file);
      fs_remove(".uploading.tmp");

      if (script_rx_pos < 1) {
        serial("-- no data received\r\n");
      } else {
        // strip \r\n
        uint8_t i = (script_rx_pos < sizeof(temp_name_buffer)) ? (uint8_t)script_rx_pos : sizeof(temp_name_buffer)-1;
        while (i > 0 &&
               (temp_name_buffer[i - 1] == '\r' || temp_name_buffer[i - 1] == '\n')) {
          i--;
          temp_name_buffer[i] = '\0';
        }
        serial("-- set filename: %s\r\n", temp_name_buffer);
        // test if filename is valid
        const int flags = LFS_O_CREAT | LFS_O_WRONLY;
        lfs_file_t file;
        if (fs_file_open(&file, temp_name_buffer, flags) == 0) {
          fs_file_close(&file);
          if (fs_file_size(&file) == 0) fs_remove(temp_name_buffer);
          strcpy(filename, temp_name_buffer);
        } else {
          serial("-- bad filename!\r\n");
        }
      }
      reset_script_rx();
    } else {
      serial("-- file name end without start\r\n");
    }
    break;

  case REPL_CMD_END_WRITE:
    if(in_rx_script) {
      serial("-- write file\r\n");
      fs_file_close(&upload_file);
      if (script_rx_pos < 1) {
        serial("-- no data received\r\n");
        fs_remove(".uploading.tmp");
      } else {
        // test compilation from file
        if (vm_test_file(".uploading.tmp")) {
          fs_remove(filename);
          fs_rename(".uploading.tmp", filename);
          serial("-- file written: %s\r\n", filename);
        } else {
          serial("-- compilation fail!\r\n");
          fs_remove(".uploading.tmp");
        }
      }
      reset_script_rx();
    } else {
      serial("-- file write end without start\r\n");
    }
    break;

  case REPL_CMD_GET_FILENAME:
    serial("-- filename: %s\r\n", filename);
    break;

  case REPL_CMD_PRINT:
    lfs_file_t rfile;
    if (fs_file_open(&rfile, filename, LFS_O_RDONLY) == 0) {
      const unsigned int sz = (unsigned int)fs_file_size(&rfile);
      if (sz > 0) {
        char *buf = malloc(sz + 1);
        buf[sz] = 0;
        fs_file_read(&rfile, buf, sz);
        serial(buf);
        free(buf);
      } else {
        serial("-- empty file\r\n");
      }
      fs_file_close(&rfile);
    } else {
      serial("-- no file\r\n");
    }
    break;

  default:
    serial("-- unknown command\r\n");
    return false;
  }
  return true;
}

void repl_handle_byte(uint8_t b) {
  if (b == '\n' || b == '\r') {
    // if (b == '\n') {
    if (line_buf[0] == '^' && line_buf[1] == '^') {
      char c = line_buf[2];
      if (c >= 97 && c <= 122) {
        // convert lowercase to uppercase ascii
        c -= 32;
      }
      if (!handle_command(c)) {
        serial("-- unknown command\r\n");
      }
      line_buf[2] = 0;
    } else {
      // not a command
      if (in_rx_script) {
        // buffer filename data or stream script lines to LittleFS
        if (script_rx_pos + line_buf_pos < (sizeof(temp_name_buffer) - 1)) {
          memcpy(temp_name_buffer + script_rx_pos, line_buf, line_buf_pos);
          temp_name_buffer[script_rx_pos + line_buf_pos] = '\n';
        }
        fs_file_write(&upload_file, line_buf, line_buf_pos);
        fs_file_write(&upload_file, "\n", 1);
        script_rx_pos += line_buf_pos + 1;
      } else {
        // compile and execute the line as lua source, printing any errors
        line_buf[line_buf_pos] = '\0';
        vm_run_buffer(line_buf);
      }
    }
    line_buf_pos = 0;
    // ECHO
    // printf("\r\n");
    return;
  }

  // ECHO
  // non-line-ending characters
  //serial("%c", b);
  if (line_buf_pos < LINE_BUFFER_SIZE) {
    line_buf[line_buf_pos++] = b;
  } else {
    // good chance we're getting sent serialosc gibberish?
    // so let's abandon it
    // serial("-- line buffer full!\r\n");
    line_buf_pos = 0;
  }
}

void repl_handle_bytes(uint8_t *data, uint32_t len) {
  if (len < 1)
    return;

  for (unsigned int i = 0; i < len; i++) {
    repl_handle_byte(data[i]);
  }
}
