#pragma once

#include <helpers/IdentityStore.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>
#include "NodePrefs.h"

class DataStoreHost {
public:
  virtual bool onContactLoaded(const ContactInfo& contact) =0;
  virtual bool getContactForSave(uint32_t idx, ContactInfo& contact) =0;
  virtual bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) =0;
  virtual bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) =0;
};

class DataStore {
  FILESYSTEM* _fs;
  FILESYSTEM* _fsExtra;
  uint8_t _prefs_load_raw = 2;   // PrefsDefaults until loadPrefs() finds a file
  mesh::RTCClock* _clock;
  IdentityStore identity_store;

  void loadPrefsInt(const char *filename, NodePrefs& prefs);
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  void checkAdvBlobFile();
#endif

public:
  DataStore(FILESYSTEM& fs, mesh::RTCClock& clock);
  DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock);
  void begin();
  bool formatFileSystem();
  FILESYSTEM* getPrimaryFS() const { return _fs; }
  FILESYSTEM* getSecondaryFS() const { return _fsExtra; }
  bool loadMainIdentity(mesh::LocalIdentity &identity);
  bool saveMainIdentity(const mesh::LocalIdentity &identity);
  void loadPrefs(NodePrefs& prefs);
  bool savePrefs(NodePrefs& prefs);
  // #1571: where loadPrefs() found them, for the GUI's boot screen.
  enum PrefsLoad : uint8_t { PrefsFromFile, PrefsMigrated, PrefsDefaults };
  PrefsLoad prefsLoad() const { return PrefsLoad(_prefs_load_raw); }
  void loadContacts(DataStoreHost* host);
  void saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c) = NULL);
  void loadChannels(DataStoreHost* host);
  void saveChannels(DataStoreHost* host);
  // #1364: whether this node has a stored channel set at all, i.e. whether loadChannels()
  // would restore anything. A caller that wants to seed channels on a FRESH node only has
  // no other way to ask: loadChannels() returns void, and `num_channels` counts what
  // addChannel() put there rather than what came off the filesystem, because the load
  // path writes slots through setChannel() and never touches that counter. Seeding without
  // this check corrupts a configured node -- seeds left in the slots past the stored ones,
  // or an addChannel() landing on top of a user's channel.
  bool hasChannels() const;
  void migrateToSecondaryFS();
  uint8_t getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]);
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len);
  bool deleteBlobByKey(const uint8_t key[], int key_len);
  File openRead(const char* filename);
  File openRead(FILESYSTEM* fs, const char* filename);
  File openWriteFile(const char* filename);   // #241: truncating write-open on the primary FS (block list)
  File openWriteFile(FILESYSTEM* fs, const char* filename);   // #1474: the same on a named FS
  bool removeFile(const char* filename);
  bool removeFile(FILESYSTEM* fs, const char* filename);
  uint32_t getStorageUsedKb() const;
  uint32_t getStorageTotalKb() const;

#ifdef OFFBAND_GUI
  // Chunked contacts save (#1458): a few records per loop tick into a temp
  // file that replaces /contacts3 only once complete, so a GUI frame never
  // waits out the whole rewrite.
  bool beginContactsSave();
  // Appends contacts [from, to) that pass `filter`; false on a write error.
  bool saveContactsSlice(DataStoreHost* host, bool (*filter)(const ContactInfo& c), uint32_t from,
                         uint32_t to);
  // Closes the temp file and swaps it in when `commit`, else discards it.
  bool finishContactsSave(bool commit);
  bool contactsSaveOpen() const { return _contacts_tmp_open; }
#endif

private:
  FILESYSTEM* _getContactsChannelsFS() const { if (_fsExtra) return _fsExtra; return _fs;};
#ifdef OFFBAND_GUI
  File _contacts_tmp;
  bool _contacts_tmp_open = false;
#endif
};
