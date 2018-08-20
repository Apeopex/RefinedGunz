#include "stdafx.h"
#include "MZip.h"
#include "zip/zlib.h"
#include "MFile.h"
#include <memory.h>
#include <string.h>
#include "MDebug.h"
#include <cassert>
#include "zlib_util.h"
#include <algorithm>
#include <vector>
#include "libzip/zip.h"
#include <string.h>
#include <stdio.h>

typedef unsigned long dword;
typedef unsigned short word;

#define MRS_ZIP_CODE	0x05030207
#define MRS2_ZIP_CODE	0x05030208

#pragma pack(2)
struct MZIPLOCALHEADER{
	enum{
		SIGNATURE   = 0x04034b50,
		SIGNATURE2  = 0x85840000,
		COMP_STORE  = 0,
		COMP_DEFLAT = 8,
	};

	dword   sig;
	word    version;
	word    flag;
	word    compression;      // COMP_xxxx
	word    modTime;
	word    modDate;
	dword   crc32;
	dword   cSize;
	dword   ucSize;
	word    fnameLen;         // Filename string follows header.
	word    xtraLen;          // Extra field follows filename.
};

struct MZIPDIRHEADER{
	enum{
		SIGNATURE = 0x06054b50,
	};

	dword   sig;
	word    nDisk;
	word    nStartDisk;
	word    nDirEntries;
	word    totalDirEntries;
	dword   dirSize;
	dword   dirOffset;
	word    cmntLen;
};

struct MZIPDIRFILEHEADER{
	enum{
		SIGNATURE   = 0x02014b50,
		SIGNATURE2  = 0x05024b80,
		COMP_STORE  = 0,
		COMP_DEFLAT = 8,
	};

	dword   sig;
	word    verMade;
	word    verNeeded;
	word    flag;
	word    compression;      // COMP_xxxx
	word    modTime;
	word    modDate;
	dword   crc32;
	dword   cSize;            // Compressed size
	dword   ucSize;           // Uncompressed size
	word    fnameLen;         // Filename string follows header.
	word    xtraLen;          // Extra field follows filename.
	word    cmntLen;          // Comment field follows extra field.
	word    diskStart;
	word    intAttr;
	dword   extAttr;
	dword   hdrOffset;

	char *GetName   () const { return (char *)(this + 1);   }
	char *GetExtra  () const { return GetName() + fnameLen; }
	char *GetComment() const { return GetExtra() + xtraLen; }
};

#pragma pack()

void ConvertChar(char* pData,int _size)
{
	if(!pData) return;

	u16 w;
	u8 b, bh;

	for(int i=0;i<_size;i++) {
		b = *pData ^ 0xFF;
		w = b<<3;
		bh = (w&0xff00)>>8;
		b = w&0xff;
		*pData = u8( b | bh );
		pData++;

	}
}

static void RecoveryChar(char* data, size_t size)
{
	for (size_t i = 0; i < size; ++i)
	{
		u8& c = reinterpret_cast<u8*>(data)[i];
		const u8 bh = c & 0x07;
		const u8 d = (bh << 5) | (c >> 3);
		c = d ^ 0xFF;
	}
}

MZip::MZip()
{
	m_fp = NULL;
	m_pDirData = NULL;
	m_ppDir = NULL;
	m_nDirEntries = 0;
	m_nZipMode = ZMode_Zip;
	m_dwReadMode = 0;
}

MZip::~MZip()
{
	Finalize();
}

bool MZip::isReadAble(u32 mode)
{
	if (m_nZipMode == ZMode_Zip) {
		return (MZIPREADFLAG_ZIP & mode) != 0;
	}
	else if (m_nZipMode == ZMode_Mrs) {
		return (MZIPREADFLAG_MRS & mode) != 0;
	}
	else if (m_nZipMode == ZMode_Mrs2) {
		return (MZIPREADFLAG_MRS2 & mode) != 0;
	}
	return false;
}

bool MZip::Initialize(FILE* fp, u32 ReadMode)
{
	if (fp == NULL) return false;
	int err = ZIP_ER_OK;
	
	int fileIdx = fileno(fp);
	
	reader = zip_fdopen(fileIdx, 0, &err);
    if (err != ZIP_ER_OK)
    {
        MLog("Error %d reading pre-opened zip file\n", err);
        return false;
    }
	
	return InitializeImpl();
}

bool MZip::Initialize(const char * File, size_t Size, u32 ReadMode)
{
    int err = ZIP_ER_OK;
	
	reader = zip_open(File, 0, &err);
    if (err != ZIP_ER_OK)
    {
        MLog("Error %d opening zip file %s\n", err, File);
        return false;
    }

	return InitializeImpl();
}

bool MZip::InitializeImpl()
{
    int err = ZIP_ER_OK;
	zip_int64_t num_entries = zip_get_num_entries(reader, 0);

	for (zip_uint64_t i = 0; i < (zip_uint64_t)num_entries; i++) {
		zip_stat_t fInfo;
		zip_stat_init(&fInfo);
		err = zip_stat_index(reader, i, 0, &fInfo);
		if (err != ZIP_ER_OK)
		{
			MLog("Error %d stating file #%d in zip file\n", err, i);
			return false;
		}
		files.push_back(fInfo);
	}
	return true;
}

bool MZip::Finalize()
{
	return true;
}

int MZip::GetFileCount(void) const
{
	return files.size();
}

void MZip::GetFileName(int i, char *szDest) const
{
	if(szDest!=NULL){
		if (i < 0 || i >= GetFileCount()){
			*szDest = '\0';
		}
		else{
			strcpy(szDest, files.at(i).name);
		}
	}
}

StringView MZip::GetFileName(int i) const
{
	if (i < 0 || i >= GetFileCount())
		return nullptr;

	const char *filename = files.at(i).name;
	return{ filename, strlen(filename) };
}

int t_strcmp(const char* str1,const char* str2)
{
	int len = strlen(str1);
	if(strlen(str2)!=len) return -1;
	
	for(int i=0;i<len;i++) {

		if(str1[i] != str2[i]) {
			if(	((str1[i]=='\\') || (str1[i]=='/')) && ((str1[i]=='\\') || (str1[i]=='/')) ) {
				continue;
			}
			else
				return -1;
		}
	}
	return 0;
}

int MZip::GetFileIndex(const char* szFileName) const
{
	if(szFileName==NULL) return -1;

	for(int i=0; i<GetFileCount();i++){
		if(t_strcmp(szFileName, files.at(i).name)==0) 
			return i;
	}

	return -1;
}

int MZip::GetFileLength(int i) const
{
	if(i<0 || i>=GetFileCount())
		return 0;
	else {
		return files.at(i).size;
	}
}

int MZip::GetFileLength(const char* filename) const
{
	int index = GetFileIndex(filename);

	if(index == -1) return 0;

	return GetFileLength(index);
}

unsigned int MZip::GetFileCRC32(int i) const
{
	if(i<0 || i>=GetFileCount())
		return 0;
	else
		return files.at(i).crc;
}

unsigned int MZip::GetFileCRC32(const char* filename) const
{
	int index = GetFileIndex(filename);

	if(index == -1) return 0;

	return GetFileCRC32(index);
}

unsigned int MZip::GetFileTime(int i) const
{
	if(i<0 || i>=GetFileCount())
		return 0;
	else
		return files.at(i).mtime;
}

unsigned int MZip::GetFileTime(const char* filename) const
{
	int index = GetFileIndex(filename);

	if(index == -1) return 0;

	return GetFileTime(index);
}

size_t MZip::GetFileArchiveOffset(int i)
{
	if (i < 0 || i >= GetFileCount())
		return 0;

	return 0;
}

size_t MZip::GetFileCompressedSize(int i) const
{
	if(i<0 || i>=GetFileCount())
		return 0;
	else
		return files.at(i).comp_size;
}

bool MZip::IsFileCompressed(int i) const
{
	if (i < 0 || i >= GetFileCount())
		return 0;

	return files.at(i).comp_method != ZIP_CM_STORE;
}

void MZip::Seek(i64 Offset, u32 Origin)
{
	
}

i64 MZip::Tell()
{
	return 0;
}

void MZip::ReadN(void* Out, size_t Size)
{
	
}

bool MZip::ReadFile(int i, void* pBuffer, int nMaxSize)
{
	if (pBuffer==NULL || i<0 || i>=GetFileCount())
		return false;

	zip_file_t *zf = zip_fopen_index(reader, i, 0);
	
	zip_int64_t bytesRead = zip_fread(zf, pBuffer, nMaxSize);
	
	zip_fclose(zf);
	
	return bytesRead >= 0;
}

bool MZip::ReadFile(const char* filename, void* pBuffer, int nMaxSize)
{
    int fIndex = GetFileIndex(filename);
	if (fIndex < 0) {
		MLog("Error %d locating file %s in zip file\n", filename);
		return false;
	}
	return ReadFile(fIndex, pBuffer, nMaxSize);
}

static char _fileheaderReader[1024*16]; // sizeof(fh) + fh.fnameLen + fh.xtraLen + fh.cmntLen
static int	_fileheaderReaderSize = 0;

bool MZip::UpgradeMrs(char* mrs_name) // Mrs To Mrs2
{
	return true;
}

bool MZip::ConvertZip(char* zip_name)
{
	return true;
}

bool MZip::RecoveryMrs(FILE* fp)
{
	return true;
}

bool MZip::RecoveryMrs2(FILE* fp)
{
	return true;
}

bool MZip::isZip()
{
	return true;
}

bool MZip::isVersion1Mrs()
{
	return false;
}

bool MZip::isZip(FILE* fp)
{
	return true;
}

bool MZip::isVersion1Mrs(FILE* fp)
{
	return false;
}

bool MZip::RecoveryZip(char* zip_name)
{
	return true;
}

/////////////////////////////////////////////////////////////////////////////////

FNode::FNode()
{
	memset(m_name,0,256);
	m_size	 = 0;
	m_offset = 0;
}

void FNode::SetName(const char* str)	
{
	if(strlen(str) > 255) return;
	strcpy_safe(m_name,str);
}

/////////////////////////////////////////////////////////////////////////////////

FFileList::FFileList()
{

}

FFileList::~FFileList() 
{
	DelAll();
}

void FFileList::Add(FNode* pNode) 
{
	push_back(pNode);
}

void FFileList::DelAll() 
{
	if(size()==0) return;

	iterator node;

	for(node = begin(); node != end(); ) 
	{
		delete (*node);
		(*node) = NULL;
		node = erase(node);
	}
}

void FFileList::UpgradeMrs() 
{
	iterator node;
	FNode* pNode = NULL;

	for(node = begin(); node != end(); ++node) 
	{
		pNode = (*node);

		if(MZip::UpgradeMrs( pNode->m_name ))
			mlog(" Upgrade mrs : %s\n",pNode->m_name);
	}
}

void FFileList::ConvertZip() 
{
	iterator node;
	FNode* pNode = NULL;

	for(node = begin(); node != end(); ++node) {
		pNode = (*node);

		if(MZip::ConvertZip( pNode->m_name ))
			mlog("convert zip : %s\n",pNode->m_name);
	}
}

void FFileList::RecoveryZip() 
{
	iterator node;
	FNode* pNode = NULL;

	for(node = begin(); node != end(); ++node) {
		pNode = (*node);

		MZip::RecoveryZip( pNode->m_name );
	}
}

#ifdef WIN32
#include <Windows.h>
#include <shellapi.h>
#ifndef _T
#define _T(x) x
#endif
void FFileList::ConvertVtf() 
{
	iterator node;
	FNode* pNode = NULL;

	char temp[1024];
	char temp_arg[1024];
	int len;

	for(node = begin(); node != end(); ++node) {
		pNode = (*node);

		strcpy_safe(temp, pNode->m_name);
		len = strlen(temp);
		temp[len-3] = 0;
		strcat_safe(temp, "tga");

		sprintf_safe(temp_arg,"%s %s",pNode->m_name,temp);
		HINSTANCE hr = ShellExecuteA(NULL, _T("open"), _T("vtf2tga.exe"),_T(temp_arg), NULL, SW_HIDE);
	}
}
#endif

void FFileList::ConvertNameMRes2Zip() 
{
	iterator node;
	FNode* pNode = NULL;

	char _buf_rename[256];
	int len;

	for(node = begin(); node != end(); ++node) {
		pNode = (*node);

		strcpy_safe(_buf_rename,pNode->m_name);
		len = (int)strlen(pNode->m_name);

		_buf_rename[len-3] = 0;
		strcat_safe(_buf_rename,"zip");

		rename( pNode->m_name, _buf_rename);

		mlog("rename : %s -> %s \n",_buf_rename,pNode->m_name);
	}
}

void FFileList::ConvertNameZip2MRes() 
{
	iterator node;
	FNode* pNode = NULL;

	char _buf_rename[256];
	int len;

	for(node = begin(); node != end(); ++node) {
		pNode = (*node);

		strcpy_safe(_buf_rename,pNode->m_name);
		len = (int)strlen(pNode->m_name);

		_buf_rename[len-3] = 0;
		strcat_safe(_buf_rename,"mrs");

		rename( pNode->m_name, _buf_rename);

		mlog("rename : %s -> %s \n",pNode->m_name,_buf_rename);
	}
}

static bool GetEntityList(const char* path, FFileList& pList, bool Dirs)
{
	FNode* pNode;

	for (auto&& File : MFile::Glob(path))
	{
		if (strcmp(File.Name, ".") == 0)	continue;
		if (strcmp(File.Name, "..") == 0)	continue;
		if (bool(File.Attributes & MFile::Attributes::Subdir) == Dirs)	continue;

		pNode = new FNode;
		pNode->SetName(File.Name);
		pList.Add(pNode);

	}

	return true;
}

bool GetDirList(const char* path, FFileList& pList)
{
	return GetEntityList(path, pList, true);
}

bool GetFileList(const char* path, FFileList& pList)
{
	return GetEntityList(path, pList, false);
}

#if _WIN32
bool GetFileListWin(const char* path, FFileList& pList)
{
	WIN32_FIND_DATA		file_t;
	HANDLE				hFile;

	FNode* pNode;

	if( (hFile = FindFirstFile( path , &file_t )) != INVALID_HANDLE_VALUE ) {

		do {

			if(strcmp(file_t.cFileName, "." )==0)					continue;
			if(strcmp(file_t.cFileName, "..")==0)					continue;
			if(file_t.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )	continue;

			pNode = new FNode;
			pNode->SetName(file_t.cFileName);
			pList.Add(pNode);

		} while( FindNextFile( hFile, &file_t ) );

		FindClose(hFile);
	}

	return true;
}

bool GetFindFileList(const char* path, const char* ext, FFileList& pList)
{
	_finddata_t file_t;
	long hFile;

	FNode* pNode;

	if( (hFile = _findfirst( path , &file_t )) != -1L ) {
		do{
			if(strcmp(file_t.name, "." )==0) continue;
			if(strcmp(file_t.name, "..")==0) continue;

			if(file_t.attrib & _A_SUBDIR ) {
				char _path[256];
				strcpy_safe(_path,file_t.name);
				strcat_safe(_path,"/");
				strcat_safe(_path,path);

				GetFindFileList(_path,ext,pList);
				continue;
			}

			int len = (int)strlen(ext);
			int filelen = (int)strlen(file_t.name);

			char* pName = &file_t.name[filelen-len];

			if(_stricmp(pName,ext)==0) {

				int len = (int)strlen(path);

				char temp_name[256];

				if(len > 3) {

					memcpy(temp_name,path,len-3);
					temp_name[len-3]=0;
					strcat_safe(temp_name,file_t.name);
				}
				else {
					strcpy_safe(temp_name,file_t.name);
				}

				pNode = new FNode;
				pNode->SetName(temp_name);
				pList.Add(pNode);

			}

		} 
		while( _findnext( hFile, &file_t ) == 0 );

		_findclose(hFile);
	}

	return true;
}

bool GetFindFileListWin(const char* path, const char* ext, FFileList& pList)
{
	WIN32_FIND_DATA		file_t;
	HANDLE				hFile;

	FNode* pNode;

	if( (hFile = FindFirstFile( path , &file_t )) != INVALID_HANDLE_VALUE ) {

		do{
			if(strcmp(file_t.cFileName, "." )==0)	continue;
			if(strcmp(file_t.cFileName, "..")==0)	continue;

			if(file_t.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )	{
				char _path[1024];

				int len = strlen(path);

				strcpy_safe(_path,path);
				_path[len-1] = 0;
				strcat_safe(_path,file_t.cFileName);
				strcat_safe(_path,"/*");

				GetFindFileListWin(_path,ext,pList);
				continue;
			}

			int len = (int)strlen(ext);
			int filelen = (int)strlen(file_t.cFileName);

			char* pName = &file_t.cFileName[filelen-len];

			if(_stricmp(pName,ext)==0) {

				int len = (int)strlen(path);

				char temp_name[1024];

				if(len > 1) {

					strncpy_safe(temp_name, path, len);
					strcat_safe(temp_name,file_t.cFileName);
				}
				else {
					strcpy_safe(temp_name,file_t.cFileName);
				}

				pNode = new FNode;
				pNode->SetName(temp_name);
				pList.Add(pNode);

			}

		} while( FindNextFile( hFile, &file_t ) );

		FindClose(hFile);
	}

	return true;
}
#endif
