#include "USBMSC.h"
#include "USB.h"
#include "FS.h"
#include "SPIFFS.h"
#include "USB_DRIVE.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define HWSerial Serial0
#define USBSerial Serial
#else
#define HWSerial Serial
USBCDC USBSerial;
#endif


  boolean newConfigAvailable=true;
  boolean shoudSave=false;
  boolean initialize_Disk=false;

// Device time in seconds since the Unix epoch (UTC). Set by NTP sync when running
// in WiFi client mode. <=0 means "no reliable clock" -> keep template timestamps.
volatile time_t g_deviceEpoch = 0;

unsigned long saveTime;


USBMSC MSC;

 uint8_t (*msc_disk)[DISK_SECTOR_SIZE] = NULL; // Disk image buffer, allocated in PSRAM
 
 uint8_t msc_init[4][DISK_SECTOR_SIZE] =
{
  //------------- Block0: Boot Sector -------------//
  {
    // Header (62 bytes)
    0xEB, 0x3C, 0x90, //jump_instruction
    'M' , 'S' , 'D' , 'O' , 'S' , '5' , '.' , '0' , //oem_name
    FAT_U16(DISK_SECTOR_SIZE), //bytes_per_sector
    FAT_U8(1),    //sectors_per_cluster
    FAT_U16(1),   //reserved_sectors_count
    FAT_U8(1),    //file_alloc_tables_num
    FAT_U16(MAX_ROOT_DIR_ENTRIES),  //max_root_dir_entries (64)
    FAT_U16(DISK_SECTOR_COUNT), //fat12_sector_num
    0xF8,         //media_descriptor
    FAT_U16(DISC_SECTORS_PER_TABLE),   //sectors_per_alloc_table;//FAT12 and FAT16
    FAT_U16(1),   //sectors_per_track;//A value of 0 may indicate LBA-only access
    FAT_U16(1),   //num_heads
    FAT_U32(0),   //hidden_sectors_count
    FAT_U32(0),   //total_sectors_32
    0x00,         //physical_drive_number;0x00 for (first) removable media, 0x80 for (first) fixed disk
    0x00,         //reserved
    0x29,         //extended_boot_signature;//should be 0x29
    FAT_U32(0x1234), //serial_number: 0x1234 => 1234
    'T' , 'i' , 'n' , 'y' , 'U' , 'S' , 'B' , ' ' , 'M' , 'S' , 'C' , //volume_label padded with spaces (0x20)
    'F' , 'A' , 'T' , '1' , '2' , ' ' , ' ' , ' ' ,  //file_system_type padded with spaces (0x20)

    // Zero up to 2 last bytes of FAT magic code (448 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

     //boot signature (2 bytes)
    0x55, 0xAA
  },

  //------------- Block1: FAT12 Table -------------//
  {
FAT_TBL2B(0xFF8, 0xFFF), 
FAT_TBL2B(0x3,0x4),
FAT_TBL2B(0x5,0x6),
FAT_TBL2B(0x7,0x8),
FAT_TBL2B(0x9,0xA),
FAT_TBL2B(0xB,0xFFF),
FAT_TBL2B(0xD,0xE),
FAT_TBL2B(0xF,0x10),
FAT_TBL2B(0x11,0x12),
FAT_TBL2B(0x13,0x14),
FAT_TBL2B(0x15,0x16),
FAT_TBL2B(0x17,0x18),
FAT_TBL2B(0x19,0x1A),
FAT_TBL2B(0x1B,0x1C),
FAT_TBL2B(0x1D,0x1E),
FAT_TBL2B(0x1F,0x20),
FAT_TBL2B(0x21,0x22),
FAT_TBL2B(0x23,0x24),
FAT_TBL2B(0x25, 0x26),
FAT_TBL2B(0x27, 0x28),
FAT_TBL2B(0x29, 0x2A),
FAT_TBL2B(0x2B, 0xFFF),


// first 2 entries must be 0xFF8 0xFFF, third entry is cluster end of readme file
  },

  //------------- Block2: Root Directory -------------//
  {
    // first entry is volume label
    'q', 'P' , 'o' , 'c' , 'k' , 'e' , 't' , 'P' , 
    'C' , 'R' , ' ' , 
    0x08, //FILE_ATTR_VOLUME_LABEL
    0x00, 
    FAT_MS2B(0,0), 
    FAT_HMS2B(0,0,0),
    FAT_YMD2B(0,0,0), 
    FAT_YMD2B(0,0,0), 
    FAT_U16(0), 
    FAT_HMS2B(13,42,30),  //last_modified_hms
    FAT_YMD2B(2018,11,5), //last_modified_ymd
    FAT_U16(0), 
    FAT_U32(0),
    
 
    // second entry is PROTOCOL.TXT
    'P' , 'R' , 'O' , 'T' , 'O' , 'C' , 'O' , 'L',//file_name[8]; padded with spaces (0x20)
    'T' , 'X' , 'T' ,     //file_extension[3]; padded with spaces (0x20)
    0x20,                 //file attributes: FILE_ATTR_ARCHIVE
    0x00,                 //ignore
    FAT_MS2B(1,980),      //creation_time_10_ms (max 199x10 = 1s 990ms)
    FAT_HMS2B(12,0,0),    //create_time_hms: 12:00:00
    FAT_YMD2B(2037,1,1),  //create_time_ymd: 2037-01-01 (FAR FUTURE)
    FAT_YMD2B(2037,1,1),  //last_access_ymd: 2037-01-01
    FAT_U16(0),           //extended_attributes
    FAT_HMS2B(12,0,0),    //last_modified_hms: 12:00:00
    FAT_YMD2B(2037,1,1),  //last_modified_ymd: 2037-01-01
    FAT_U16(2),           //start of file in cluster (PROTOCOL)
    FAT_U32(sizeof(PROTOCOL_TEMPLATE)-1), //file size


    // third entry is WIFI.TXT (WiFi configuration template)
    'W' , 'I' , 'F' , 'I' , '.' , 'T' , 'X' , 'T',//file_name[8]
    0x20,                 //file_extension[3]; padded with spaces (0x20)
    0x20,                 //file attributes: FILE_ATTR_ARCHIVE
    0x00,                 //ignore
    FAT_MS2B(1,980),      //creation_time_10_ms
    FAT_HMS2B(12,0,0),    //create_time_hms: 12:00:00
    FAT_YMD2B(2037,1,1),  //create_time_ymd: 2037-01-01 (FAR FUTURE)
    FAT_YMD2B(2037,1,1),  //last_access_ymd: 2037-01-01
    FAT_U16(0),           //extended_attributes
    FAT_HMS2B(12,0,0),    //last_modified_hms: 12:00:00
    FAT_YMD2B(2037,1,1),  //last_modified_ymd: 2037-01-01
    FAT_U16(WIFI_TXT_START_CLUSTER), //start of file in cluster (WIFI.TXT)
    FAT_U32(sizeof(WIFI_TEMPLATE)-1), //file size


    // fourth entry is DATAQPCR.TXT (moved after WIFI.TXT)
    'D' , 'A' , 'T' , 'A' , 'Q' , 'P' , 'C' , 'R',//file_name[8]; padded with spaces (0x20)
    'T' , 'X' , 'T' ,     //file_extension[3]; padded with spaces (0x20)
    0x20,                 //file attributes: FILE_ATTR_ARCHIVE
    0x00,                 //ignore
    FAT_MS2B(1,980),      //creation_time_10_ms (max 199x10 = 1s 990ms)
    FAT_HMS2B(12,0,0),    //create_time_hms: 12:00:00
    FAT_YMD2B(2037,1,1),  //create_time_ymd: 2037-01-01 (FAR FUTURE)
    FAT_YMD2B(2037,1,1),  //last_access_ymd: 2037-01-01
    FAT_U16(0),           //extended_attributes
    FAT_HMS2B(12,0,0),    //last_modified_hms: 12:00:00
    FAT_YMD2B(2037,1,1),  //last_modified_ymd: 2037-01-01
    FAT_U16(DATAQPCR_START_CLUSTER), //start of file in cluster (DATAQPCR)
    FAT_U32(0), //file size



  },

  //------------- Block3: Readme Content -------------//
  PROTOCOL_TEMPLATE
};


void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.print (file.name());
            time_t t= file.getLastWrite();
            struct tm * tmstruct = localtime(&t);
            Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n",(tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday,tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
            if(levels){
                listDir(fs, file.path(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.print(file.size());
            time_t t= file.getLastWrite();
            struct tm * tmstruct = localtime(&t);
            Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n",(tmstruct->tm_year)+1900,( tmstruct->tm_mon)+1, tmstruct->tm_mday,tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
        }
        file = root.openNextFile();
    }
}


void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if(!file){
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();
}


void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}


uint16_t extract12BitNumber(int index) {
  // Calculate the starting index in the byte array
  int byteIndex = (index * 3) / 2; // Dividing by 2 because each 12-bit number occupies 1.5 bytes
  
  // Determine whether to take the most significant or least significant bits
  bool takeMSB = index % 2 == 0; // If index is even, take most significant bits; otherwise, take least significant bits
  
  // Combine bytes to form 12-bit number
  uint16_t twelveBitNumber;
   if (takeMSB) {
    twelveBitNumber = (msc_disk[1][byteIndex]) | ((msc_disk[1][byteIndex + 1]& 0x0F) << 8);
  } else {
    twelveBitNumber = (msc_disk[1][byteIndex]&0xf0)>>4 | msc_disk[1][byteIndex+1]<<4;
  }
  
  return twelveBitNumber;
}


// Helper: get pointer to 32-byte directory entry across the root directory sectors (sectors 2..FIRST_DATA_SECTOR-1)
static inline uint8_t* getRootDirEntryPtr(int entryIndex)
{
  int sector = 2 + (entryIndex / 16);
  int offset = (entryIndex % 16) * 32;
  return &msc_disk[sector][offset];
}

// Forward declaration: stamp a FAT12 directory-entry timestamp from a Unix epoch.
static void setEntryTimestamp(uint8_t* entry, time_t epoch);

// Find directory entry index in root directory (0..MAX_ROOT_DIR_ENTRIES-1)
// Returns entry index or -1 if not found
static int findRootDirEntry(const char* name8)
{
  for (int i = 0; i < MAX_ROOT_DIR_ENTRIES; i++) {
    uint8_t* entry = getRootDirEntryPtr(i);
    uint8_t firstByte = entry[0];
    if (firstByte == 0x00) break; // End of directory
    if (firstByte == 0xE5) continue; // Deleted entry
    uint8_t attr = entry[11];
    if ((attr & 0x18) == 0) { // Regular file (not volume label 0x08, not dir 0x10)
      if (memcmp(entry, name8, 8) == 0) {
        return i;
      }
    }
  }
  return -1;
}

String getConfig()
{
  int config_index = findRootDirEntry("PROTOCOL");
  String configString = "";
  
  if (config_index >= 0) {
    uint8_t* entry = getRootDirEntryPtr(config_index);
    int config_length = entry[28] | (entry[29] << 8);
    int config_cluster = entry[26] | (entry[27] << 8);

    int maxConfigLength = (DATAQPCR_START_CLUSTER - PROTOCOL_START_CLUSTER) * DISK_SECTOR_SIZE;
    if (config_length > maxConfigLength) config_length = maxConfigLength;

    int clusterLength = 0;
    for (int cluster = 0; cluster < (config_length / DISK_SECTOR_SIZE + 1); cluster++) {
      if (config_cluster < 2 || config_cluster >= DISK_SECTOR_COUNT) break;
      uint8_t* config_pointer = &msc_disk[CLUSTER_TO_SECTOR(config_cluster)][0];

      clusterLength = config_length - DISK_SECTOR_SIZE * cluster;
      if (clusterLength > DISK_SECTOR_SIZE) clusterLength = DISK_SECTOR_SIZE;
      if (clusterLength <= 0) break;

      char newString[clusterLength + 1];
      strncpy(newString, (const char*)config_pointer, clusterLength);
      newString[clusterLength] = '\0';
      configString += String(newString);

      config_cluster = extract12BitNumber(config_cluster);
      if (config_cluster >= 0xFF8) break; // End of cluster chain
    }
  }
  return configString;
}

void setFat12Entry(int index, uint16_t value)
{
  uint8_t* fat = msc_disk[1];
  int offset = (index * 3) / 2;

  if (index % 2 == 0)
  {
    // even index: value fills byte[offset] and the low nibble of byte[offset+1]
    fat[offset] = value & 0xFF;
    fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
  }
  else
  {
    // odd index: value fills the high nibble of byte[offset] and all of byte[offset+1]
    fat[offset] = ((value << 4) & 0xF0) | (fat[offset] & 0x0F);
    fat[offset + 1] = (value >> 4) & 0xFF;
  }
}

void buildFatTable()
{
  // Clear the entire FAT sector (marks all clusters free by default: 0x000)
  memset(msc_disk[1], 0, DISK_SECTOR_SIZE);

  // Entries 0 and 1 are reserved by FAT specification
  setFat12Entry(0, 0xFF8);
  setFat12Entry(1, 0xFFF);

  // 1. Allocate clusters for PROTOCOL.TXT if present, according to its actual size
  int protoIdx = findRootDirEntry("PROTOCOL");
  if (protoIdx >= 0) {
    uint8_t* entry = getRootDirEntryPtr(protoIdx);
    uint16_t startCluster = entry[26] | (entry[27] << 8);
    uint32_t size = entry[28] | (entry[29] << 8) | (entry[30] << 16) | (entry[31] << 24);

    if (startCluster >= 2 && startCluster < DISK_SECTOR_COUNT - 1) {
      int clusters = (size + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;
      if (clusters < 1) clusters = 1;
      int maxClusters = DATAQPCR_START_CLUSTER - PROTOCOL_START_CLUSTER;
      if (clusters > maxClusters) clusters = maxClusters;

      for (int i = 0; i < clusters - 1; i++) {
        setFat12Entry(startCluster + i, startCluster + i + 1);
      }
      setFat12Entry(startCluster + clusters - 1, 0xFFF);
    }
  }

  // 2. Allocate clusters for WIFI.TXT if present (fixed capacity, not size-based)
  int wifiIdx = findRootDirEntry("WIFI");
  if (wifiIdx >= 0) {
    uint8_t* entry = getRootDirEntryPtr(wifiIdx);
    uint16_t startCluster = entry[26] | (entry[27] << 8);

    if (startCluster == WIFI_TXT_START_CLUSTER &&
        startCluster < DATAQPCR_START_CLUSTER - 1) {
      int clusters = DATAQPCR_START_CLUSTER - WIFI_TXT_START_CLUSTER; // fixed 8 clusters
      for (int i = 0; i < clusters - 1; i++) {
        setFat12Entry(startCluster + i, startCluster + i + 1);
      }
      setFat12Entry(startCluster + clusters - 1, 0xFFF);
    }
  }

  // 3. Allocate clusters for DATAQPCR.TXT if present, according to its actual size
  int dataIdx = findRootDirEntry("DATAQPCR");
  if (dataIdx >= 0) {
    uint8_t* entry = getRootDirEntryPtr(dataIdx);
    uint16_t startCluster = entry[26] | (entry[27] << 8);
    uint32_t size = entry[28] | (entry[29] << 8) | (entry[30] << 16) | (entry[31] << 24);

    if (startCluster >= 2 && startCluster < DISK_SECTOR_COUNT - 1) {
      int clusters = (size + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;
      if (clusters < 1) clusters = 1;
      int maxClusters = DISK_SECTOR_COUNT - 2 - startCluster + 1;
      if (clusters > maxClusters) clusters = maxClusters;

      for (int i = 0; i < clusters - 1; i++) {
        setFat12Entry(startCluster + i, startCluster + i + 1);
      }
      setFat12Entry(startCluster + clusters - 1, 0xFFF);
    }
  }
}

void init_msc_disk()
{
  // Copy boot sector (sector 0)
  memcpy(msc_disk[0], msc_init[0], DISK_SECTOR_SIZE);

  // Clear all root directory sectors (sectors 2..FIRST_DATA_SECTOR-1)
  for (int sector = 2; sector < FIRST_DATA_SECTOR; sector++) {
    memset(msc_disk[sector], 0, DISK_SECTOR_SIZE);
  }

  // Copy initial directory entries (volume label, PROTOCOL.TXT, DATAQPCR.TXT) into sector 2
  memcpy(msc_disk[2], msc_init[2], DISK_SECTOR_SIZE);

  // Clear all data sectors starting from FIRST_DATA_SECTOR
  for (int sector = FIRST_DATA_SECTOR; sector < DISK_SECTOR_COUNT; sector++) {
    memset(msc_disk[sector], 0, DISK_SECTOR_SIZE);
  }

  // Write PROTOCOL_TEMPLATE into the cluster 2 data sector
  int protoSector = CLUSTER_TO_SECTOR(PROTOCOL_START_CLUSTER);
  memcpy(msc_disk[protoSector], PROTOCOL_TEMPLATE, sizeof(PROTOCOL_TEMPLATE) - 1);

  // Build the FAT12 table
  buildFatTable();
}

void addFileToFAT(fs::FS &fs, String path){
   uint8_t* c = &msc_disk[DATAQPCR_START_SECTOR][0];

   File file = fs.open(path);
   if (!file) {
     Serial.println("Failed to open file for reading");
     return;
   }

   Serial.printf("Reading file: %s\n", path.c_str());

   unsigned long maxFileSize = (DISK_SECTOR_COUNT - DATAQPCR_START_SECTOR) * DISK_SECTOR_SIZE;
   unsigned long i = 0;
   while (file.available() && i < maxFileSize) {
     c[i] = file.read();
     i++;
   }
   file.close();

   int dataIdx = findRootDirEntry("DATAQPCR");
   if (dataIdx >= 0) {
     uint8_t* entry = getRootDirEntryPtr(dataIdx);
     entry[28] = FAT_U8(i);
     entry[29] = FAT_U8(i >> 8);
     entry[30] = FAT_U8(i >> 16);
     entry[31] = FAT_U8(i >> 24);
     // Stamp last-modified time with the current device clock (no-op if unset).
     setEntryTimestamp(entry, g_deviceEpoch);
   }

   buildFatTable();
}

void addProtoToFAT(String str){
   uint8_t* c = &msc_disk[CLUSTER_TO_SECTOR(PROTOCOL_START_CLUSTER)][0];

   unsigned long maxProtoSize = (DATAQPCR_START_CLUSTER - PROTOCOL_START_CLUSTER) * DISK_SECTOR_SIZE;
   unsigned long str_length = str.length();
   if (str_length > maxProtoSize) str_length = maxProtoSize;

   for (unsigned long i = 0; i < str_length; i++) {
     c[i] = str[i];
   }

   int protoIdx = findRootDirEntry("PROTOCOL");
   if (protoIdx >= 0) {
     uint8_t* entry = getRootDirEntryPtr(protoIdx);
     entry[28] = FAT_U8(str_length);
     entry[29] = FAT_U8(str_length >> 8);
     entry[30] = FAT_U8(str_length >> 16);
     entry[31] = FAT_U8(str_length >> 24);
     // Stamp last-modified time with the current device clock (no-op if unset).
     setEntryTimestamp(entry, g_deviceEpoch);
   }

   buildFatTable();
}

void addStringToFAT(String str){
   uint8_t* c = &msc_disk[DATAQPCR_START_SECTOR][0];

   int dataIdx = findRootDirEntry("DATAQPCR");
   unsigned long offset = 0;
   if (dataIdx >= 0) {
     uint8_t* entry = getRootDirEntryPtr(dataIdx);
     offset = entry[28] | (entry[29] << 8) | (entry[30] << 16) | (entry[31] << 24);
   }

   unsigned long maxFileSize = (DISK_SECTOR_COUNT - DATAQPCR_START_SECTOR) * DISK_SECTOR_SIZE;
   unsigned long newSize = offset + str.length();
   if (newSize > maxFileSize) newSize = maxFileSize;

   for (unsigned long i = 0; i < str.length() && (offset + i) < newSize; i++) {
     c[offset + i] = str[i];
   }

   if (dataIdx >= 0) {
     uint8_t* entry = getRootDirEntryPtr(dataIdx);
     entry[28] = FAT_U8(newSize);
     entry[29] = FAT_U8(newSize >> 8);
     entry[30] = FAT_U8(newSize >> 16);
     entry[31] = FAT_U8(newSize >> 24);
     // Stamp last-modified time with the current device clock (no-op if unset).
     setEntryTimestamp(entry, g_deviceEpoch);
   }

   buildFatTable();
}



void saveBinToSPIFFS(uint8_t binArray[],size_t binSize,const char* filename) {
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.print("Failed to open file for writing: ");
        Serial.println(filename);

        return;
    }

    // Write the array to the file

if( file.write((uint8_t *)binArray, binSize)){
        Serial.println("- file written");
    } else {
        Serial.println("- write failed");
    }

    
    file.close();
}

bool loadBinFromSPIFFS(uint8_t binArray[], size_t binSize, const char* filename) {
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return false; // Return false indicating failure
    }

    // Read the array from the file
    size_t bytesRead = file.readBytes((char *)binArray, binSize);
    
    file.close();

    // Check if read operation was successful
    if (bytesRead != binSize) {
        Serial.println("Error: Incomplete read");
        return true; // Return true indicating failure
    }

    return false; // Return flase indicating success
}


// ================= Packed mask (mask.bin) =================
// The camera correction mask has 76800 pixels (SENS_WIDTH 640 x SENS_HEIGHT 120),
// each a single ON/OFF bit. Storing it packed saves ~66KB vs one byte per pixel.
// On-disk layout: byte[0] = MASK_MAGIC, bytes[1..9600] hold the bits MSB-first.
// The in-RAM maskBuf stays a plain boolean[] array; only the on-disk form is packed.

#define MASK_PIXELS (640 * 120)   // SENS_WIDTH 640 x SENS_HEIGHT 120 = 76800
#define MASK_PACKED_BYTES ((MASK_PIXELS + 7) / 8) // 9600
#define MASK_MAGIC 0xA7

void saveMaskToSPIFFS(uint8_t *maskBuf)
{
    // The packed buffer is ~9.6 KB. It must NOT be a stack local (would overflow
    // the 8 KB Arduino loop/setup task stack) nor a static one (would overflow
    // dram0.bss). Allocate it on the heap, preferring PSRAM, and free it after.
    uint8_t *packed = (uint8_t *)heap_caps_malloc(MASK_PACKED_BYTES + 1, MALLOC_CAP_SPIRAM);
    if (packed == NULL) packed = (uint8_t *)malloc(MASK_PACKED_BYTES + 1);
    if (packed == NULL) {
        Serial.println("saveMaskToSPIFFS: out of memory");
        return;
    }

    memset(packed, 0, MASK_PACKED_BYTES + 1);
    packed[0] = MASK_MAGIC;
    for (int i = 0; i < MASK_PIXELS; i++) {
        if (maskBuf[i]) packed[1 + (i / 8)] |= (0x80 >> (i % 8));
    }

    File file = SPIFFS.open("/mask.bin", FILE_WRITE);
    if (!file) {
        Serial.println("saveMaskToSPIFFS: failed to open for writing");
        free(packed);
        return;
    }
    file.write((uint8_t *)packed, MASK_PACKED_BYTES + 1);
    esp_task_wdt_reset();
    file.close();
    free(packed);
    Serial.printf("saveMaskToSPIFFS: wrote %d bytes (magic 0x%02X)\n", MASK_PACKED_BYTES + 1, MASK_MAGIC);
}

bool loadMaskFromSPIFFS(uint8_t *maskBuf)
{
    File file = SPIFFS.open("/mask.bin", FILE_READ);
    if (!file) {
        Serial.println("loadMaskFromSPIFFS: mask.bin not found");
        return true; // failure
    }

    // The packed buffer is ~9.6 KB. It must NOT be a stack local (would overflow
    // the 8 KB Arduino loop/setup task stack right after "Baseline loaded") nor a
    // static one (would overflow dram0.bss). Use the heap, preferring PSRAM.
    uint8_t *packed = (uint8_t *)heap_caps_malloc(MASK_PACKED_BYTES + 1, MALLOC_CAP_SPIRAM);
    if (packed == NULL) packed = (uint8_t *)malloc(MASK_PACKED_BYTES + 1);
    if (packed == NULL) {
        Serial.println("loadMaskFromSPIFFS: out of memory");
        file.close();
        return true; // failure
    }

    size_t bytesRead = file.readBytes((char *)packed, MASK_PACKED_BYTES + 1);
    file.close();

    if (bytesRead != MASK_PACKED_BYTES + 1) {
        Serial.println("loadMaskFromSPIFFS: incomplete read");
        free(packed);
        return true; // failure
    }

    // Backward compatibility: an old byte-packed mask.bin has no magic byte.
    // If the first byte is not the magic, treat the whole file as legacy and
    // fall back to a fresh initMask() so the device still works.
    if (packed[0] != MASK_MAGIC) {
        Serial.println("loadMaskFromSPIFFS: bad magic - ignoring legacy mask.bin");
        free(packed);
        return true; // failure -> caller will re-init mask
    }

    for (int i = 0; i < MASK_PIXELS; i++) {
        maskBuf[i] = (packed[1 + (i / 8)] & (0x80 >> (i % 8))) != 0;
    }
    free(packed);
    Serial.printf("loadMaskFromSPIFFS: loaded %d pixels\n", MASK_PIXELS);
    return false; // success
}




void saveMscToSPIFFS(uint8_t array[DISK_SECTOR_COUNT][DISK_SECTOR_SIZE]) {
    File file = SPIFFS.open("/my_array.bin", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    // Write the array to the file in chunks, feeding the watchdog in between
    // (a full 128KB write can take a few seconds on SPIFFS)
    const size_t chunkSize = 16384;
    for (size_t offset = 0; offset < DISK_SECTOR_COUNT * DISK_SECTOR_SIZE; offset += chunkSize) {
        file.write((uint8_t *)array + offset, chunkSize);
        esp_task_wdt_reset();
    }
    
    file.close();
}

bool readMscFromSPIFFS(uint8_t array[DISK_SECTOR_COUNT][DISK_SECTOR_SIZE]) {
    File file = SPIFFS.open("/my_array.bin", FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return false;
    }

    // Read the array from the file
    file.readBytes((char *)array, DISK_SECTOR_COUNT * DISK_SECTOR_SIZE);
    
    file.close();
    return true;
}


static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize){
  HWSerial.printf("MSC WRITE: lba: %u, offset: %u, bufsize: %u\n", lba, offset, bufsize);
  memcpy(msc_disk[lba] + offset, buffer, bufsize);
  Serial.print("write");
  //  Serial.println(bufsize);
           shoudSave=true;      

  return bufsize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize){

  HWSerial.printf("MSC READ: lba: %u, offset: %u, bufsize: %u\n", lba, offset, bufsize);
  memcpy(buffer, msc_disk[lba] + offset, bufsize);
    Serial.println("read");

  return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject){
  HWSerial.printf("MSC START/STOP: power: %u, start: %u, eject: %u\n", power_condition, start, load_eject);
      Serial.println("start stop");
         shoudSave=true;
         saveTime=millis();

         // On eject, persist the disk image to SPIFFS immediately so that a
         // "remove device" cannot lose an unwritten protocol/data file. This is
         // independent of cameraOn (which otherwise blocks Service_USB()).
         if (load_eject) {
             saveMscToSPIFFS(msc_disk);
             shoudSave=false;
             Serial.println("save-on-eject");
         }
  return true;
}

static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
  if(event_base == ARDUINO_USB_EVENTS){
    arduino_usb_event_data_t * data = (arduino_usb_event_data_t*)event_data;
    switch (event_id){
      case ARDUINO_USB_STARTED_EVENT:
        HWSerial.println("USB PLUGGED");
        break;
      case ARDUINO_USB_STOPPED_EVENT:
        HWSerial.println("USB UNPLUGGED");
        break;
      case ARDUINO_USB_SUSPEND_EVENT:
        HWSerial.printf("USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en);
        break;
      case ARDUINO_USB_RESUME_EVENT:
        HWSerial.println("USB RESUMED");
        break;
      
      default:
        break;
    }
  }
}


// ================= WIFI.TXT configuration (USB disk) =================
// The host PC edits WIFI.TXT on the virtual USB drive. It is an INI-style file:
//   NAME: <free text, ignored by firmware>
//   SSID=<your access point name>
//   PASSWORD=<your access point password>
// If both SSID and PASSWORD are non-empty AND the device can connect to that
// AP, it boots in Client mode. Otherwise (empty fields or connection failed)
// it falls back to Access Point mode. When no WIFI.TXT exists yet, a template
// with empty SSID/PASSWORD is created so the user has something to edit.

char wifi_config_ssid[65]      = {0};   // max 64 chars + NUL
char wifi_config_password[65]  = {0};   // max 64 chars + NUL

// Read a single "KEY=value" line from buf into out (trailing \r stripped).
// Surrounding whitespace around key and value is trimmed so that lines such as
// "SSID = MyHome" work the same as "SSID=MyHome".
static void parseWifiLine(const char* buf, String& key, String& value)
{
  char* eq = strchr(buf, '=');
  if (eq == NULL) { key = ""; value = ""; return; }
  int len = eq - buf;
  String k = String(buf).substring(0, len);
  String v = String(buf).substring(len + 1);

  // Trim leading/trailing spaces and tabs from the key.
  int ks = 0, ke = k.length();
  while (ks < ke && (k[ks] == ' ' || k[ks] == '\t')) ks++;
  while (ke > ks && (k[ke-1] == ' ' || k[ke-1] == '\t')) ke--;
  key = k.substring(ks, ke - ks);

  // Trim leading/trailing spaces and tabs from the value.
  int vs = 0, ve = v.length();
  while (vs < ve && (v[vs] == ' ' || v[vs] == '\t')) vs++;
  while (ve > vs && (v[ve-1] == ' ' || v[ve-1] == '\t')) ve--;
  value = v.substring(vs, ve - vs);

  // Strip trailing CR/LF from the value.
  while (value.length() > 0 && (value[value.length()-1] == '\r' || value[value.length()-1] == '\n'))
    value.remove(value.length() - 1);
}

// Parse WIFI.TXT content from the USB disk image into wifi_config_ssid / password.
void readWifiConfig()
{
  wifi_config_ssid[0]      = '\0';
  wifi_config_password[0]  = '\0';

  int wifiIdx = findRootDirEntry("WIFI");
  if (wifiIdx < 0) {
    // No WIFI.TXT yet - caller will create a template.
    return;
  }

  uint8_t* entry = getRootDirEntryPtr(wifiIdx);
  int config_length = entry[28] | (entry[29] << 8);
  int config_cluster = entry[26] | (entry[27] << 8);

  // Capacity of the WIFI.TXT region is fixed.
  int maxConfigLength = (DATAQPCR_START_CLUSTER - WIFI_TXT_START_CLUSTER) * DISK_SECTOR_SIZE;
  if (config_length > maxConfigLength) config_length = maxConfigLength;
  if (config_length <= 0) return;

  // Read the whole file content into a temporary buffer. Use the heap (PSRAM
  // preferred): a stack buffer would overflow the 8 KB task stack and a static
  // one would overflow dram0.bss.
  char *buf = (char *)heap_caps_malloc(maxConfigLength + 1, MALLOC_CAP_SPIRAM);
  if (buf == NULL) buf = (char *)malloc(maxConfigLength + 1);
  if (buf == NULL) {
    Serial.println("readWifiConfig: out of memory");
    return;
  }
  int total = 0;
  int cluster = config_cluster;
  while (total < maxConfigLength && cluster >= 2 && cluster < DISK_SECTOR_COUNT) {
    uint8_t* ptr = &msc_disk[CLUSTER_TO_SECTOR(cluster)][0];
    int chunk = DISK_SECTOR_SIZE;
    if (total + chunk > maxConfigLength) chunk = maxConfigLength - total;
    memcpy(buf + total, ptr, chunk);
    total += chunk;
    if (cluster == WIFI_TXT_START_CLUSTER) break; // single-cluster file
    cluster = extract12BitNumber(cluster);
    if (cluster >= 0xFF8) break;
  }
  buf[total] = '\0';

  // Parse KEY=value lines.
  String line, key, value;
  int pos = 0;
  while (pos < total) {
    char* nl = (char*)memchr(buf + pos, '\n', total - pos);
    if (nl == NULL) { nl = buf + total; } else { *nl = '\0'; nl = buf + (nl - buf); }
    line = String(buf + pos);
    parseWifiLine(line.c_str(), key, value);
    if (key.equalsIgnoreCase("SSID")) {
      strncpy(wifi_config_ssid, value.c_str(), sizeof(wifi_config_ssid) - 1);
      wifi_config_ssid[sizeof(wifi_config_ssid) - 1] = '\0';
    } else if (key.equalsIgnoreCase("PASSWORD")) {
      strncpy(wifi_config_password, value.c_str(), sizeof(wifi_config_password) - 1);
      wifi_config_password[sizeof(wifi_config_password) - 1] = '\0';
    }
    pos += nl - (buf + pos); // advance past this line
  }
  free(buf);
}

// Create an empty WIFI.TXT template in the USB disk image if one does not exist.
void createWifiConfigTemplate()
{
  int wifiIdx = findRootDirEntry("WIFI");
  if (wifiIdx >= 0) {
    // Existing file - keep it as-is.
    return;
  }

  // Find a free directory slot (first zero byte).
  int freeSlot = -1;
  for (int i = 0; i < MAX_ROOT_DIR_ENTRIES; i++) {
    uint8_t* e = getRootDirEntryPtr(i);
    if (e[0] == 0x00) { freeSlot = i; break; }
  }
  if (freeSlot < 0) return; // no room

  uint8_t* entry = getRootDirEntryPtr(freeSlot);
  memset(entry, 0, 32);
  memcpy(entry, "WIFI.TXT", 8);   // bytes 0-7: filename
  entry[11] = 0x20;              // byte 11: attributes (archive)

  // Timestamps / dates (FAT_YMD2B/FAT_HMS2B each emit two bytes).
  entry[4]  = FAT_U8(1980);      // creation time low (placeholder)
  entry[5]  = FAT_HMS2B(12,0,0); // create_time_hms
  entry[6]  = FAT_YMD2B(2037,1,1);// create_time_ymd
  entry[7]  = FAT_YMD2B(2037,1,1);// last_access_ymd
  entry[8]  = 0;                 // extended attributes (high byte of start cluster)
  entry[9]  = FAT_HMS2B(12,0,0); // last_modified_hms
  entry[10] = FAT_YMD2B(2037,1,1);// last_modified_ymd

  // Start cluster (bytes 26-27) and file size (bytes 28-31).
  entry[26] = FAT_U8(WIFI_TXT_START_CLUSTER);
  entry[27] = 0;
  const char* tmpl = WIFI_TEMPLATE;
  int tlen = strlen(tmpl);
  entry[28] = FAT_U8(tlen);
  entry[29] = FAT_U8(tlen >> 8);
  entry[30] = FAT_U8(tlen >> 16);
  entry[31] = FAT_U8(tlen >> 24);

  // Write template content into the WIFI.TXT cluster.
  uint8_t* c = &msc_disk[CLUSTER_TO_SECTOR(WIFI_TXT_START_CLUSTER)][0];
  memset(c, 0, DISK_SECTOR_SIZE);
  memcpy(c, tmpl, tlen);

  // If an old disk image (pre-WIFI.TXT layout) is present, its DATAQPCR.TXT
  // directory entry still points at cluster DATAQPCR_START_CLUSTER-8 (the old
  // first data cluster). Shift it forward by the WIFI.TXT region width so the
  // two files do not collide. buildFatTable() then rebuilds the FAT chains.
  int dataIdx = findRootDirEntry("DATAQPCR");
  if (dataIdx >= 0) {
    uint8_t* de = getRootDirEntryPtr(dataIdx);
    uint16_t oldStart = de[26] | (de[27] << 8);
    int shift = DATAQPCR_START_CLUSTER - WIFI_TXT_START_CLUSTER; // 8 clusters
    if (oldStart == DATAQPCR_START_CLUSTER - shift) {
      de[26] = FAT_U8(WIFI_TXT_START_CLUSTER + shift);
      de[27] = 0;
      Serial.println("WIFI.TXT: migrated old DATAQPCR.TXT cluster offset");
    }
  }

  buildFatTable();
}

// Write a FAT12 directory-entry timestamp (standard layout) from a Unix epoch time.
//   byte[4]  = creation tenths of second
//   byte[5..6]  = creation time (hms packed)
//   byte[7..8]  = creation date (ymd packed)
//   byte[9..10] = last access date (ymd packed)
//   byte[11..12] = last modification time (hms packed)
//   byte[13..14] = last modification date (ymd packed)
static void setEntryTimestamp(uint8_t* entry, time_t epoch)
{
  if (epoch <= 0) return;   // no reliable clock -> keep template timestamp
  struct tm tmbuf;
  struct tm* ptm = gmtime_r(&epoch, &tmbuf);
  if (!ptm) return;

  int tenths = (ptm->tm_sec % 10) * 10;   // FAT stores only the tens digit of seconds
  entry[4]  = FAT_U8(tenths);
  entry[5]  = FAT_HMS2B(ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
  entry[7]  = FAT_YMD2B(ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
  entry[9]  = FAT_YMD2B(ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
  entry[11] = FAT_HMS2B(ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
  entry[13] = FAT_YMD2B(ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
}

// Stamp every regular-file directory entry (PROTOCOL / WIFI / DATAQPCR) with the
// current UTC time derived from g_deviceEpoch. No-op when g_deviceEpoch <= 0, so
// AP mode and WiFi-disabled builds keep their fixed template timestamps.
void applyNtpTimestamps()
{
  if (g_deviceEpoch <= 0) return;

  for (int i = 0; i < MAX_ROOT_DIR_ENTRIES; i++) {
    uint8_t* entry = getRootDirEntryPtr(i);
    if (!entry[0]) break;                 // end of directory
    if (entry[0] == 0xE5) continue;       // deleted slot
    if ((entry[11] & 0x18) != 0) continue;// skip volume label / directories
    setEntryTimestamp(entry, g_deviceEpoch);
  }
}

void Start_USB_Drive()
{
  HWSerial.begin(250000);
  HWSerial.setDebugOutput(true);

  // Allocate the disk image buffer in PSRAM when available (fallback: internal RAM)
  if (msc_disk == NULL) {
    msc_disk = (uint8_t(*)[DISK_SECTOR_SIZE]) heap_caps_calloc(DISK_SECTOR_COUNT, DISK_SECTOR_SIZE, MALLOC_CAP_SPIRAM);
    if (msc_disk == NULL) {
      Serial.println("PSRAM not available - using internal RAM for USB disk");
      msc_disk = (uint8_t(*)[DISK_SECTOR_SIZE]) calloc(DISK_SECTOR_COUNT, DISK_SECTOR_SIZE);
    }
  }

  if (initialize_Disk)
  {
    init_msc_disk();
    saveMscToSPIFFS(msc_disk);
  }
  else
  {
    if (!readMscFromSPIFFS(msc_disk)) {
      Serial.println("Disk image not found, creating a fresh disk");
      init_msc_disk();
      saveMscToSPIFFS(msc_disk);
    } else {
      // Check if the loaded disk has old layout (e.g. max_root_dir_entries != MAX_ROOT_DIR_ENTRIES)
      uint16_t rootEntries = msc_disk[0][17] | (msc_disk[0][18] << 8);
      if (rootEntries != MAX_ROOT_DIR_ENTRIES) {
        Serial.println("Old disk layout detected (not 64 entries). Upgrading disk image...");
        init_msc_disk();
        saveMscToSPIFFS(msc_disk);
      }
    }
    buildFatTable();
  }
  addStringToFAT("Start Run to get Data");


  USB.onEvent(usbEventCallback);
  MSC.vendorID("ESP32");//max 8 chars
  MSC.productID("USB_MSC");//max 16 chars
  MSC.productRevision("1.0");//max 4 chars
  MSC.onStartStop(onStartStop);
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.begin(DISK_SECTOR_COUNT, DISK_SECTOR_SIZE);
  USBSerial.begin();
  USB.begin();

}


void Service_USB()
{
  // Persist the disk image to SPIFFS once per write/eject event. The previous
  // !cameraOn guard was removed: camera state is irrelevant to the USB data
  // files, and leaving it in blocked saves after a measurement run (which can
  // leave the camera running), causing "device removal" to lose unwritten data.
  if (shoudSave && ((millis()-saveTime)>1000)){saveMscToSPIFFS(msc_disk);shoudSave=false;      Serial.println("save");
   newConfigAvailable=true;
   saveTime=millis();
}

  }


void InitializeUSBFiles()
{

  readMscFromSPIFFS(msc_disk);
  String protoString=getConfig();
  init_msc_disk();
  addProtoToFAT(protoString);


}
void InitializeUSB()
{
SPIFFS.begin(true);

bool formatted = SPIFFS.format();

MSC.mediaPresent(false);
delay(500);
initialize_Disk=true;
Start_USB_Drive();
initialize_Disk=false;
  }


void SPIFF_Format()
{
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("\n\n----Listing files before format----");
  bool formatted = SPIFFS.format();
  if(formatted){
    Serial.println("\n\nSuccess formatting");
  }else{
    Serial.println("\n\nError formatting");
  }
  Serial.println("\n\n----Listing files after format----");
}
  
