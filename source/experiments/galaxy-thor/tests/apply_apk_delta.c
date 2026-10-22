/* Reconstruct a signed APK into a separate staging file; never edit an installed APK. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const char* message) { fprintf(stderr, "%s: %s\n", message, strerror(errno)); exit(1); }
static void read_all(FILE* f, void* p, size_t n) { if (fread(p,1,n,f)!=n) fail("Short patch input"); }
int main(int argc,char** argv) {
 if(argc!=4 || strcmp(argv[3],"/data/local/tmp/wiids-galaxy-new.apk")) return 2;
 FILE* old=fopen(argv[1],"rb"); FILE* patch=fopen(argv[2],"rb");
 if(!old||!patch)fail("Open input");
 struct stat statbuf; if(fstat(fileno(old),&statbuf))fail("Stat input");
 unsigned char magic[8];uint64_t target;uint32_t count;
 read_all(patch,magic,8);read_all(patch,&target,8);read_all(patch,&count,4);
 if(memcmp(magic,"WDSDEL1\0",8)||target>UINT64_C(3000000000)||count>100000) return 3;
 int fd=open(argv[3],O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600); if(fd<0)fail("Open staging output");
 FILE* out=fdopen(fd,"wb");if(!out)fail("Output stream");
 uint64_t written=0;unsigned char buffer[65536];
 for(uint32_t i=0;i<count;i++) {
  unsigned char kind;uint64_t offset,length;
  read_all(patch,&kind,1);read_all(patch,&offset,8);read_all(patch,&length,8);
  if(kind>1||length>target-written) return 4;
  FILE* input=patch;
  if(kind==1) {
   if(offset>(uint64_t)statbuf.st_size||length>(uint64_t)statbuf.st_size-offset)return 5;
   if(fseeko(old,(off_t)offset,SEEK_SET))fail("Seek baseline");input=old;
  }
  uint64_t remaining=length;
  while(remaining){size_t n=remaining>sizeof buffer?sizeof buffer:(size_t)remaining;
   read_all(input,buffer,n);if(fwrite(buffer,1,n,out)!=n)fail("Write staging output");remaining-=n;}
  written+=length;
 }
 if(written!=target||fgetc(patch)!=EOF)return 6;
 if(fflush(out)||fsync(fd))fail("Flush staging output");
 fclose(out);fclose(patch);fclose(old);
 printf("Reconstructed %llu bytes; verify SHA-256 before installation.\n",(unsigned long long)written);
 return 0;
}
