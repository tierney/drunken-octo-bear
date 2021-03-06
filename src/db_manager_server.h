// Database manager class for Lockbox.
//
//  // Example for how to create a new top_dir directory.
//  lockbox::DBManager::Options options;
//  options.type = lockbox::ServerDB::TOP_DIR_META;
//  CreateGUIDString(&(options.name));
//  manager.Track(options);
//
//  string new_top_dir;
//  CreateGUIDString(&new_top_dir);
//  manager.Put(options, "name_key", "name_value");
//

#pragma once

#include <mutex>
#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "counter.h"
#include "db_manager.h"
#include "leveldb/db.h"
#include "lockbox_types.h"
#include "update_queue_sync.h"

using std::mutex;
using std::string;

namespace lockbox {

class DBManagerServer : public DBManager {
 public:
  DBManagerServer(const string& db_location_base);

  virtual ~DBManagerServer();

  bool Get(const Options& options, const string& key, string* value);

  bool Put(const Options& options, const string& key, const string& value);

  bool Append(const Options& options,
              const string& key,
              const string& new_value);

  bool NewTopDir(const Options& options);

  bool Track(const Options& options);

  uint64_t MaxID(const Options& options);

  // Creates pathhs of the name /a/path/to/the/dest/THIS_IS_A_TYPE/OPTIONS_NAME.
  string GenPath(const string& base, const Options& options);

  // Creates keys of the name THIS_IS_A_TYPE and THIS_IS_A_TYPEOPTIONS_NAME.
  string GenKey(const Options& options);

  UserID GetNextUserID();
  DeviceID GetNextDeviceID();
  TopDirID GetNextTopDirID();

  mutex* get_mutex(const Options& options);

  UserID EmailToUserID(const string& email);
  bool AddEmailToTopDir(const string& email, const TopDirID& top_dir);

 private:
  void InitTopDirs();

  Counter num_users_;
  Counter num_devices_;
  Counter num_top_dirs_;

  map<string, mutex*> db_mutex_;
  Sync* sync_;

  DISALLOW_COPY_AND_ASSIGN(DBManagerServer);
};

} // namespace lockbox
