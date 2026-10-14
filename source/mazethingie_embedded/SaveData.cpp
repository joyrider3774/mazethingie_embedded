#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "Platform.h"
#include "Commonvars.h"
#include "SaveData.h"

// ===========================================================================
// Saved data, kept in the platform's save storage
//
// Everything is one record, stamped with a magic value and a version and ended
// by a CRC over the bytes before it. That is what tells a never written sector,
// an older layout or a half finished write apart from real data. Anything that
// does not check out is replaced by the defaults rather than trusted.
// ===========================================================================

#define SAVE_MAGIC        0x4448  
#define SAVE_VERSION      1
#define STORE_SAVE_ADDR   0

typedef struct SaveRecord SaveRecord;
struct SaveRecord
{
	uint16_t magic;
	uint8_t  version;
	uint8_t w;
	uint8_t h;
	uint16_t crc;                         //covers every byte before it
};

#define STORE_TOTAL (STORE_SAVE_ADDR + sizeof(SaveRecord))

//the record has to fit in what the platform stores
static_assert(STORE_TOTAL <= PLATFORM_STORAGE_SIZE, "the saved record does not fit in PLATFORM_STORAGE_SIZE");
//record addresses, lengths and the loops over them are uint8_t
static_assert(STORE_TOTAL <= 255, "store offsets do not fit in uint8_t");

//CRC16 CCITT, small and more than enough to spot a corrupted record
static uint16_t SaveCrc(const uint8_t* data, uint8_t len)
{
	uint16_t crc = 0xFFFF;
	while (len--)
	{
		crc ^= (uint16_t)(*data++) << 8;
		for (uint8_t bit = 0; bit < 8; bit++)
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
	}
	return crc;
}

static void defaultSaveState(void)
{
	MazeWidth = MaxMazeWidth;
	MazeHeight = MaxMazeHeight;
}

//true when the storage held a record we can trust, the values are only taken over then
static bool loadSaveState(void)
{
	SaveRecord rec;
	uint8_t* bytes = (uint8_t*)&rec;
	Platform_StorageRead(STORE_SAVE_ADDR, bytes, sizeof(SaveRecord));

	if ((rec.magic != SAVE_MAGIC) || (rec.version != SAVE_VERSION) ||
		(rec.crc != SaveCrc(bytes, offsetof(SaveRecord, crc))))
		return false;

	MazeWidth = rec.w;
	MazeHeight = rec.h;
	return true;
}

void saveSaveState(void)
{
	SaveRecord rec;
	//padding (if any) has to be the same every time, it is part of the crc
	memset(&rec, 0, sizeof(rec));
	rec.magic = SAVE_MAGIC;
	rec.version = SAVE_VERSION;
	rec.w = MazeWidth;
	rec.h = MazeHeight;
	rec.crc = SaveCrc((const uint8_t*)&rec, offsetof(SaveRecord, crc));

	//saving an unchanged record costs no write at all
	Platform_StorageWrite(STORE_SAVE_ADDR, (const uint8_t*)&rec, sizeof(SaveRecord));
}

//puts anything out of range back to its default, true if something had to change
static bool validateSaveState(void)
{
	bool changed = false;

	if ((MazeWidth > MaxMazeWidth) || (MazeWidth < 10))
	{
		changed = true;
		MazeWidth = MaxMazeWidth;
	}

	if ((MazeHeight > MaxMazeHeight) || (MazeHeight < 10))
	{
		changed = true;
		MazeHeight = MaxMazeHeight;
	}

	return changed;
}

void initSaveState(void)
{
	//never written, written by an older layout or damaged, start clean so the
	//next save has something valid to build on
	if (!loadSaveState())
	{
		defaultSaveState();
		saveSaveState();
	}
	else if (validateSaveState())
		saveSaveState();
}

