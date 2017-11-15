/*
 * Owen Khoury. Machine Problem 4
 * November 18th, 2017
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

// STRUCTS

typedef struct DIRENTRY
{
  uint8_t DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t DIR_NTRes;
  uint8_t DIR_CrtTimeTenth;
  uint16_t DIR_crtTime;
  uint16_t DIR_CrtDate;
  uint16_t DIR_LstAccDate;
  uint16_t DIR_FstClusHI;
  uint16_t DIR_WrtTime;
  uint16_t DIR_WrtDate;
  uint16_t DIR_FstClusLO;
  uint32_t DIR_FileSize;
  
}__attribute__((packed)) DIRENTRY;

typedef struct fat_BS
{
  unsigned char bootjmp[3];
  unsigned char oem_name[8];
  unsigned short bytes_per_sector;
  unsigned char sectors_per_cluster;
  unsigned short reserved_sector_count;
  unsigned char table_count;
  unsigned short root_entry_count;
  unsigned short total_sectors_16;
  unsigned char media_type;
  unsigned short table_size_16;
  unsigned short sectors_per_track;
  unsigned short head_side_count;
  unsigned int hidden_sector_count;
  unsigned int total_sectors_32;

  //this will be cast to it's specific type once the driver actually knows what type of FAT this is.
  unsigned char extended_section[54];
 
}__attribute__((packed)) fat_BS;


typedef struct fat_extBS_32
{
  //extended fat32 stuff
  unsigned int    table_size_32;
  unsigned short    extended_flags;
  unsigned short    fat_version;
  unsigned int    root_cluster;
  unsigned short    fat_info;
  unsigned short    backup_BS_sector;
  unsigned char     reserved_0[12];
  unsigned char   drive_number;
  unsigned char     reserved_1;
  unsigned char   boot_signature;
  unsigned int    volume_id;
  unsigned char   volume_label[11];
  unsigned char   fat_type_label[8];
 
}__attribute__((packed)) fat_extBS_32_t;


// GLOBAL VARIABLES

int initialized = 0;
int clusterSize;
int fatSize;
int tempCombine;
int fd = -1;
int firstDataSector;

int* fatTable;

fat_BS fat;

// Reference to the root directory and the current working directory.
DIRENTRY *dirRoot;
DIRENTRY *cwd;

// Stores pointers for each cluster.
DIRENTRY* dirTable[128];


void removeClusters(uint32_t combine) {

  uint32_t numClusters = 0;
  uint32_t tmp = combine;
  uint32_t clusterSize = fat.sectors_per_cluster * fat.bytes_per_sector;

  // Determine overall number of clusters
  do {
    tmp = fatTable[tmp] & 0x0FFFFFFF;
    numClusters++;
  } while (tmp < 0x0FFFFFF8);

  char*  clusterBlock = (char*)malloc(numClusters * clusterSize);
  uint32_t clusterItr = 1;

  int j;
  for (j = 0; j < numClusters * clusterSize; j++)
    clusterBlock[j] = 0x00;

  // Iterate through the clusters. Seek to the last one. 
  do {
    uint32_t FirstSectorofCluster = ((combine - 2) * fat.sectors_per_cluster) + firstDataSector;
    int dataOffset = FirstSectorofCluster * fat.bytes_per_sector;
    lseek(fd, dataOffset, 0);

    write(fd, &clusterBlock[(clusterItr - 1) * clusterSize], clusterSize);

    combine = fatTable[combine] & 0x0FFFFFFF;
    clusterItr++;
    
  } while (combine < 0x0FFFFFF8);

}


/*
 * Read all of the clusters for a directory.
*/
DIRENTRY* readClusters(uint32_t combine) {
  
  uint32_t numClusters = 0;
  uint32_t tmp = combine;
  uint32_t clusterSize = fat.sectors_per_cluster * fat.bytes_per_sector;

  // Determine overall number of clusters
  do {
    tmp = fatTable[tmp] & 0x0FFFFFFF;
    numClusters++;
  } while (tmp < 0x0FFFFFF8);

  char*  clusterBlock = (char*)malloc(numClusters * clusterSize);
  uint32_t clusterItr = 1;

  // Iterate through the clusters. Seek to the last one. 
  do {
    uint32_t FirstSectorofCluster = ((combine - 2) * fat.sectors_per_cluster) + firstDataSector;
    int dataOffset = FirstSectorofCluster * fat.bytes_per_sector;
    lseek(fd, dataOffset, 0);
    read(fd, &clusterBlock[(clusterItr - 1) * clusterSize], clusterSize);

    combine = fatTable[combine] & 0x0FFFFFFF;
    clusterItr++;
    
  } while (combine < 0x0FFFFFF8);

  return (DIRENTRY*)clusterBlock;
}


/*
 * Return 1 if the two directories have the same name.
 */
int compareDirNames(char* dir1, char* dir2) {
 // dir1 = name of the directory we are searching for
  
  char dir2Temp[11];
  
  int j = 0;
  while (dir2[j] != 0) {
    dir2Temp[j] = dir2[j];
    j++;
  }
  
  while (j < strlen(dir2)) {
   dir2Temp[j] = 0x20;
   j++;
  }
  
  int i;
  for (i = 0; i < strlen(dir1) - 1; i++) {
   
    if (dir1[i] != dir2Temp[i])  
      return 0;
  }
  
  return 1;
}


/**
 * Find the offset of the root directory and store the root directory values in dirRoot.
 */
void initialize() {
  
  const char* env = getenv("FAT_FS_PATH");
  
  fd = open("sampledisk32.raw", O_RDWR, 0);
  
  read(fd, (char*)&fat, sizeof(fat));
  
  if (fat.total_sectors_16 != 0) {
    fatSize = fat.table_size_16;
  }
  else {
   fat_extBS_32_t *temp = (fat_extBS_32_t*)fat.extended_section;
   fatSize = temp->table_size_32;
   tempCombine = temp->root_cluster;
  }
  
  int rootDirSectors = ((fat.root_entry_count * 32) + (fat.bytes_per_sector - 1)) / fat.bytes_per_sector;
  
  // Start of the data region.
  firstDataSector = fat.reserved_sector_count + (fat.table_count * fatSize) + rootDirSectors;
 
  int dataOffset = firstDataSector - rootDirSectors;
  clusterSize = fat.bytes_per_sector * fat.sectors_per_cluster;
  dataOffset = dataOffset * fat.bytes_per_sector;
  
  // Seek the file descriptor past the reserved section and read in the fat table.
  lseek(fd, fat.reserved_sector_count * fat.bytes_per_sector, 0);
  fatTable = (int*)malloc(fat.bytes_per_sector * fatSize);
  read(fd, fatTable, fat.bytes_per_sector*fatSize);
  
  dirRoot = readClusters(tempCombine);
  
  // Set the current working directory to root.
  cwd = dirRoot;
  
  initialized = 1;
}


/*
 * Get the first directory/file in a path.
 */
char* getFirstElement(char *path) {
  if (path == NULL)
    return NULL;
  
  int start = path[0] == '/' ? 1 : 0;
  
  int i;
  for (i = start; i < strlen(path); i++) {
    if (path[i] == '/') {
     char firstElement[i+1];
     
     strncpy(firstElement, path, i);
     firstElement[i] = '\0';
     
     return firstElement + start;
    }
  }
  return path + start;
}


/*
 * Get remaining path after current dir/file.
 */
char* getRemaining(char *path) {
  
  int end;
  
  if (path[strlen(path) -1] == '/')
    end = strlen(path)-1;
  else
    end = strlen(path);
  
  int i;
  for (i = 1; i < end; i++) {
    if (path[i] == '/') {
     return path + i;
    }
  }
  return NULL;
}


int OS_rm (const char* path) {
  if (initialized == 0)
    initialize();

  // Check if absolute or relative path.
  int absolutePathName = path[0] == '/' ?  1 : 0;

  DIRENTRY* tempDir;
  tempDir = absolutePathName ? dirRoot : cwd;

  char* tmpPath = malloc(strlen(path) + 1);
  tmpPath = strncpy(tmpPath, path, strlen(path));

  int found = -1;
  unsigned int i = 0;

  uint32_t combine;

  while (getFirstElement(tmpPath) != NULL) {

    found = -1;

    while (tempDir[i].DIR_Name[0] != '\0') {


      // Check if the current name in the path is a file.
      if (tempDir[i].DIR_Attr != 0x0F && tempDir[i].DIR_Attr != 0x10 && tempDir[i].DIR_Attr != 0x08 
            && compareDirNames(getFirstElement(tmpPath), tempDir[i].DIR_Name))
      {

        // Invalid path, the file should be the last name in the path given.
        if (getRemaining(tmpPath) != NULL)
          return -1;

        // Clear out this file and return 1 for success.
        combine = ((unsigned int) tempDir[i].DIR_FstClusHI << 16) + ((unsigned int) tempDir[i].DIR_FstClusLO);
        removeClusters(combine);
        initialize();
        return 1;
      }


      if (tempDir[i].DIR_Name[0] == 0xE5) {
        i++;
      }
      else {
        if (compareDirNames(getFirstElement(tmpPath), tempDir[i].DIR_Name)) {

          // There is no file given, the user entered a directory to be removed instead of a file.
          if (getRemaining(tmpPath) == NULL)
            return -2;

          found = 1;
          tmpPath = getRemaining(tmpPath);
          break;
        }
      }

      i++;
    }

    if (found == -1)
      return -1;

    // Get pointer to where the next cluster is.
    combine = ((unsigned int) tempDir[i].DIR_FstClusHI << 16) + ((unsigned int) tempDir[i].DIR_FstClusLO);
      
    tempDir = readClusters(combine);
  }

  return -1;
}


/*
 * Function to read files into a buffer.
 */
int OS_read(int filedes, void* buf, int nbyte, int offset) {
  
  if (initialized == 0)
    initialize();
  
  if (dirTable[filedes] == NULL)
    return -1;
    
  DIRENTRY* dir = dirTable[filedes];
  
  int clusterNum = dir->DIR_FstClusLO;
  int firstSecOfCluster = ((clusterNum -2) *fat.sectors_per_cluster) + firstDataSector;
  
  int maxClusSize = fat.bytes_per_sector * fat.sectors_per_cluster;
  
  int fatSecNum;
  int fatOffset; 
  
  char fatBuffer[fat.bytes_per_sector];
  
  char file[dir->DIR_FileSize];
  
  int fileIndex = 0;
  
  int bytesRead = -1;
  
  int bytesRemaining = dir->DIR_FileSize;
  
  int hexSector;
  
  while (bytesRemaining > 0) {
    
      if (bytesRemaining > maxClusSize)
  bytesRead = maxClusSize;
      else
  bytesRead = bytesRemaining;
      
      // Load data
      lseek(fd, firstSecOfCluster * fat.bytes_per_sector, 0);
      read(fd, &file[fileIndex], bytesRead);
      
      bytesRemaining -= bytesRead;
      fileIndex += bytesRead;
      
      // Determine next sector
      fatSecNum = ((clusterNum * 4) / fat.bytes_per_sector) + fat.reserved_sector_count;
      fatOffset = (clusterNum * 4) % fat.bytes_per_sector;
      
      lseek(fd, fatSecNum * fat.bytes_per_sector, 0);
      read(fd, fatBuffer, fat.bytes_per_sector);
      
      hexSector = fatBuffer[fatOffset + 1] << 16 | fatBuffer[fatOffset];
      clusterNum = (int)hexSector;
      
      firstSecOfCluster = ((clusterNum - 2) * fat.sectors_per_cluster) + firstDataSector;
  }

   if (fileIndex < dir->DIR_FileSize)
    return -1;
  
  memcpy(buf, (void*)&file[offset], nbyte);
  
  return fileIndex;
}


/*
 * Open a file into dirTable. return the index of the table if successfull. -1 if failed.
 */
int OS_open(const char *path) {
  if (initialized == 0)
    initialize();
  
  // Check if the path is absolute or relative.
  int absolutePathName = path[0] == '/' ?  1 : 0;
  
  // Create a temp DIRENTRY, so that we do not change the current working dir for invalid input.
  DIRENTRY* tempDir;
  
  if (absolutePathName)
    tempDir = dirRoot;
  else
    tempDir = cwd;
  
  char* tmpPath = malloc(strlen(path) + 1);
  tmpPath = strncpy(tmpPath, path, strlen(path));
  
  unsigned int i = 0;
  
  int found = -1;
  
  // While there are still directories in the path.
  while(getFirstElement(tmpPath) != NULL) {
    
     found = -1;
    
      // While there are still directories in the cluster.
      while (tempDir[i].DIR_Name[0] != '\0') {
  
   if (tempDir[i].DIR_Attr != 0x0F && tempDir[i].DIR_Attr != 0x10 && tempDir[i].DIR_Attr != 0x08 
     && compareDirNames(getFirstElement(tmpPath), tempDir[i].DIR_Name)) 
   { 
      int j;
      for (j = 0; j < 128; j++) {
    if (dirTable[j] == NULL) {
      
        dirTable[j] = &tempDir[i];
        return j;
    }
      }
   }
    
    if (tempDir[i].DIR_Name[i] == 0xE5) {
      i++;
    }
    else {
      
      if (compareDirNames(getFirstElement(tmpPath), tempDir[i].DIR_Name)) {
        
        found = 1;
        
        tmpPath = getRemaining(tmpPath);
        
        break;
      }
      
      i++;
    }
      }
      
      if (found == -1)
  return -1;
      
      // Get pointer to where the next cluster is.
      uint32_t combine = ((unsigned int) tempDir[i].DIR_FstClusHI << 16) + ((unsigned int) tempDir[i].DIR_FstClusLO);
      
      tempDir = readClusters(combine);
  }
  
  return -1;
}



int main() {

  //printf("firstElement: %s\n", getFirstElement("/file"));

  //printf("remaining: %s\n", getRemaining("file"));

  initialize();

  if(dirRoot != NULL){
    int i;
    for(i=0; i<512; i++){
      if(dirRoot[i].DIR_Name[0] != '\0'&& dirRoot[i].DIR_Attr != 0x0F){
        printf("This entry name is %.*s\n",11,(char*)(dirRoot[i].DIR_Name));
        printf("----------------------\n");
      } 
    }
  }

  //printf("\n");

  OS_rm("/CONGRATSTXT");

  initialize();

  if(dirRoot != NULL){
    int i;
    for(i=0; i<512; i++){
      if(dirRoot[i].DIR_Name[0] != '\0'&& dirRoot[i].DIR_Attr != 0x0F){
        printf("This entry name is %.*s\n",11,(char*)(dirRoot[i].DIR_Name));
        printf("----------------------\n");
      } 
    }
  }

  int x = OS_open("/CONGRATSTXT");

  char buf[342];

  OS_read(x, buf, 0, 342);

  printf("\n content: %s\n", buf);


  return 0;
}



