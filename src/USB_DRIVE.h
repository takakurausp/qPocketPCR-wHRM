#include "SPIFFS.h"

#define FAT_U8(v) ((v) & 0xFF)
#define FAT_U16(v) FAT_U8(v), FAT_U8((v) >> 8)
#define FAT_U32(v) FAT_U8(v), FAT_U8((v) >> 8), FAT_U8((v) >> 16), FAT_U8((v) >> 24)
#define FAT_MS2B(s,ms)    FAT_U8(((((s) & 0x1) * 1000) + (ms)) / 10)
#define FAT_HMS2B(h,m,s)  FAT_U8(((s) >> 1)|(((m) & 0x7) << 5)),      FAT_U8((((m) >> 3) & 0x7)|((h) << 3))
#define FAT_YMD2B(y,m,d)  FAT_U8(((d) & 0x1F)|(((m) & 0x7) << 5)),    FAT_U8((((m) >> 3) & 0x1)|((((y) - 1980) & 0x7F) << 1))
#define FAT_TBL2B(l,h)    FAT_U8(l), FAT_U8(((l >> 8) & 0xF) | ((h << 4) & 0xF0)), FAT_U8(h >> 4)

#define PROTOCOL_TEMPLATE "NAME: Protocol Template\n DATE: 13.8.2025\n \n PROTOCOL: \n  \n REPEAT: 2-4\n CYCLES: 35\n \n \n  STEP 1: Initial step\n    TEMPERATURE: 95C\n    DURATION: 12 min\n    \n  STEP 2: Denaturation\n    TEMPERATURE: 94°C\n    DURATION: 20 sec\n \n  STEP 3: Annealing\n    TEMPERATURE: 65°C\n    DURATION: 15s  \n\n  STEP 4: Extension\n    TEMPERATURE: 72°C\n    DURATION: 45s \n    CAPTURE: yes\n    \n  STEP 5: Final Step\n    TEMPERATURE: 20°C\n    DURATION: 10 min"
  

static const uint32_t DISK_SIZE=128; // Virtual USB disk size in KB (max ~160 with a single FAT sector)

static const uint32_t DISK_SECTOR_COUNT = 2 * DISK_SIZE; // 8KB is the smallest size that windows allow to mount
static const uint16_t DISK_SECTOR_SIZE = 512;    // Should be 512
static const uint16_t DISC_SECTORS_PER_TABLE = 1; //each table sector can fit 170KB (340 sectors)

#define MAX_ROOT_DIR_ENTRIES 64
#define ROOT_DIR_SECTORS     ((MAX_ROOT_DIR_ENTRIES * 32 + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE) // 4 sectors
#define FIRST_DATA_SECTOR    (1 + 1 + ROOT_DIR_SECTORS) // 1 (boot) + 1 (FAT) + 4 (root) = sector 6
#define CLUSTER_TO_SECTOR(c) (FIRST_DATA_SECTOR + (c) - 2)

// Layout of the pre-allocated (chained) files on the FAT12 disk.
#define PROTOCOL_START_CLUSTER 2
#define PROTOCOL_END_CLUSTER 31          // PROTOCOL.TXT capacity: 15 KB (adjust here to trade space with DATAQPCR.TXT)
#define DATAQPCR_START_CLUSTER (PROTOCOL_END_CLUSTER + 1)
#define DATAQPCR_START_SECTOR  CLUSTER_TO_SECTOR(DATAQPCR_START_CLUSTER)

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject);
static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);


extern boolean newConfigAvailable;

extern  uint8_t (*msc_disk)[DISK_SECTOR_SIZE]; // Allocated in PSRAM (fallback: internal RAM)

extern boolean cameraOn;
void Start_USB_Drive();


void Service_USB();

String getConfig();

void addFileToFAT(fs::FS &fs, String path);

void InitializeUSBFiles();

void InitializeUSB();

void SPIFF_Format();

void saveBinToSPIFFS(uint8_t binArray[],size_t binSize,const char* filename);

bool readMscFromSPIFFS(uint8_t array[DISK_SECTOR_COUNT][DISK_SECTOR_SIZE]);

bool loadBinFromSPIFFS(uint8_t binArray[], size_t binSize, const char* filename);
