#include "SdCard.h"

#if defined(PIN_SD_CS)

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <ff.h>
#include <sdmmc_cmd.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>

#include <MeshLog.h>

using offband::gui::MountOutcome;

#ifndef SD_SPI_KHZ
  #define SD_SPI_KHZ 20000
#endif

namespace {

sdmmc_host_t hostConfig() {
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;   // the panel's bus; GuiPanel initialized it
  host.max_freq_khz = SD_SPI_KHZ;
  return host;
}

sdspi_device_config_t deviceConfig() {
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.host_id = SPI2_HOST;
  dev.gpio_cs = gpio_num_t(PIN_SD_CS);
  return dev;
}

// Whenever the driver lets go of the card (a failed mount, an unmount, the
// raw bring-up in format()), its chip-select goes back to a plain output
// held high. The bus is shared with the panel and, on the T-Deck, the radio:
// a card whose CS is left floating can answer on MISO during their transfers
// (#1728). The next mount routes the pin to the SPI driver again.
void parkCs() {
  const gpio_num_t cs = gpio_num_t(PIN_SD_CS);
  gpio_reset_pin(cs);
  gpio_set_direction(cs, GPIO_MODE_OUTPUT);
  gpio_set_level(cs, 1);
}

}  // namespace

static const char* readFsType(sdmmc_card_t* card);

MountOutcome SdCard::mount() {
  if (_card) return MountOutcome::Ok;
  sdmmc_host_t host = hostConfig();
  sdspi_device_config_t dev = deviceConfig();
  esp_vfs_fat_mount_config_t cfg = {};
  cfg.format_if_mount_failed = false;   // never erase a card unasked
  cfg.max_files = 4;
  cfg.allocation_unit_size = 16 * 1024;
  const esp_err_t e = esp_vfs_fat_sdspi_mount(kMount, &host, &dev, &cfg, &_card);
  if (e == ESP_OK) {
    _fs_type = readFsType(_card);
    return MountOutcome::Ok;
  }
  _card = nullptr;
  parkCs();
  mesh_log_line(MLOG_BOOT, "[sd] mount: %s\n", esp_err_to_name(e));
  switch (e) {
    // Nothing answered the init sequence: an empty slot, cheap to ask again.
    case ESP_ERR_TIMEOUT:
    case ESP_ERR_INVALID_RESPONSE:
    case ESP_ERR_INVALID_CRC:
    case ESP_ERR_NOT_FOUND:
    case ESP_ERR_NOT_SUPPORTED:
      return MountOutcome::NoCard;
    // ESP_FAIL: a card answered but its volume won't mount. Anything else
    // (out of memory, a driver in the wrong state) isn't an empty slot either,
    // and must back off rather than be retried every few seconds.
    default:
      return MountOutcome::Unreadable;
  }
}

bool SdCard::probe() { return _card && sdmmc_get_status(_card) == ESP_OK; }

void SdCard::unmount() {
  if (!_card) return;
  esp_vfs_fat_sdcard_unmount(kMount, _card);
  _card = nullptr;
  parkCs();
}

bool SdCard::usage(uint64_t* total, uint64_t* free_bytes) {
  if (!_card) return false;
  FATFS* fs = nullptr;
  DWORD free_clusters = 0;
  const BYTE pdrv = ff_diskio_get_pdrv_card(_card);
  if (pdrv == 0xFF) return false;
  char drv[3] = {char('0' + pdrv), ':', 0};
  if (f_getfree(drv, &free_clusters, &fs) != FR_OK || !fs) return false;
  const uint64_t cluster = uint64_t(fs->csize) * 512u;
  if (total) *total = uint64_t(fs->n_fatent - 2) * cluster;
  if (free_bytes) *free_bytes = uint64_t(free_clusters) * cluster;
  return true;
}

bool SdCard::describe(uint64_t* capacity, const char** fs_type) {
  if (!_card) return false;
  if (capacity) *capacity = uint64_t(_card->csd.capacity) * uint64_t(_card->csd.sector_size);
  if (fs_type) *fs_type = _fs_type;
  return true;
}

// The volume's type, once per mount. f_getfree is the call that hands back
// the FATFS; on a card without a valid FSINFO its first call scans the FAT,
// and FatFs then caches the free count, so usage() stays cheap after this.
static const char* readFsType(sdmmc_card_t* card) {
  FATFS* fs = nullptr;
  DWORD free_clusters = 0;
  const BYTE pdrv = ff_diskio_get_pdrv_card(card);
  if (pdrv == 0xFF) return "FAT";
  char drv[3] = {char('0' + pdrv), ':', 0};
  if (f_getfree(drv, &free_clusters, &fs) != FR_OK || !fs) return "FAT";
  switch (fs->fs_type) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
    case FS_EXFAT: return "exFAT";
    default: return "FAT";
  }
}

// ESP-IDF 4.4 has no esp_vfs_fat_sdcard_format(), so this is its sequence by
// hand: bring the card up raw on the bus, register it with FatFs, f_mkfs,
// then let go and mount normally.
bool SdCard::format() {
  unmount();
  sdmmc_host_t host = hostConfig();
  if (sdspi_host_init() != ESP_OK) return false;
  sdspi_device_config_t dev = deviceConfig();
  sdspi_dev_handle_t handle;
  if (sdspi_host_init_device(&dev, &handle) != ESP_OK) {
    sdspi_host_deinit();
    parkCs();
    return false;
  }
  host.slot = handle;
  sdmmc_card_t* card = static_cast<sdmmc_card_t*>(calloc(1, sizeof(sdmmc_card_t)));
  bool ok = card && sdmmc_card_init(&host, card) == ESP_OK;
  BYTE pdrv = 0xFF;
  if (ok) ok = ff_diskio_get_drive(&pdrv) == ESP_OK && pdrv != 0xFF;
  if (ok) {
    ff_diskio_register_sdmmc(pdrv, card);
    char drv[3] = {char('0' + pdrv), ':', 0};
    const size_t work_len = 4096;
    void* work = malloc(work_len);
    ok = work && f_mkfs(drv, FM_ANY, 16 * 1024, work, work_len) == FR_OK;
    free(work);
    ff_diskio_unregister(pdrv);
  }
  free(card);
  sdspi_host_remove_device(handle);
  sdspi_host_deinit();
  parkCs();
  mesh_log_line(MLOG_BOOT, "[sd] format %s\n", ok ? "ok" : "FAILED");
  return ok && mount() == MountOutcome::Ok;
}

// --------------------------------------------------------------------- files

bool SdFile::open(const char* path) {
  close();
  _f = fopen(path, "r+b");
  if (!_f) _f = fopen(path, "w+b");
  return _f != nullptr;
}

void SdFile::close() {
  if (_f) fclose(_f);
  _f = nullptr;
}

bool SdFile::readAt(uint32_t offset, void* data, size_t len) {
  return _f && fseek(_f, long(offset), SEEK_SET) == 0 && fread(data, 1, len, _f) == len;
}

bool SdFile::writeAt(uint32_t offset, const void* data, size_t len) {
  return _f && fseek(_f, long(offset), SEEK_SET) == 0 && fwrite(data, 1, len, _f) == len;
}

uint32_t SdFile::size() {
  if (!_f) return 0;
  struct stat st;
  fflush(_f);
  return fstat(fileno(_f), &st) == 0 ? uint32_t(st.st_size) : 0;
}

bool SdFile::flush() { return _f && fflush(_f) == 0 && fsync(fileno(_f)) == 0; }

#endif  // PIN_SD_CS
