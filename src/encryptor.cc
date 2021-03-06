#include "encryptor.h"

#include <string>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/sha1.h"
#include "crypto/random.h"
#include "crypto/encryptor.h"
#include "crypto/rsa_private_key.h"
#include "crypto/symmetric_key.h"
#include "crypto/openssl_util.h"
#include "base/file_util.h"
#include "block_cipher.h"
#include "rsa_public_key_openssl.h"
#include "rsa.h"
#include "util.h"
#include "hash_util.h"
#include "compressor.h"

using std::string;
using std::vector;

namespace lockbox {

const int kNumEncryptAttempts = 3;

Encryptor::Encryptor(Client* client, DBManagerClient* dbm, UserAuth* user_auth)
    : client_(client), dbm_(dbm), user_auth_(user_auth) {
  CHECK(client);
  CHECK(dbm);
  CHECK(user_auth);
}

Encryptor::~Encryptor() {
}

bool Encryptor::Encrypt(const string& top_dir_path, const string& path,
                        const vector<string>& users, RemotePackage* package) {
  // Read file to string.
  string raw_input;
  file_util::ReadFileToString(base::FilePath(path), &raw_input);

  return EncryptString(top_dir_path, path, raw_input, users, package);
}


bool Encryptor::EncryptString(const string& top_dir_path,
                              const string& path,
                              const string& raw_input,
                              const vector<string>& users,
                              RemotePackage* package) {
  CHECK(package);
  bool success = false;

  // Encrypt the relative path name.
  string rel_path(RemoveBaseFromInput(top_dir_path, path));
  string dec_rel_path;
  int i = 0;
  for (i = 0; i < kNumEncryptAttempts; i++) {
    success = EncryptInternal(rel_path, users, &(package->path.data),
                              &(package->path.user_enc_session));
    package->path.data_sha1 = SHA1Hex(package->path.data);
    dec_rel_path.clear();
    CHECK(Decrypt(package->path.data,
                  package->path.user_enc_session,
                  &dec_rel_path));
    if (dec_rel_path == rel_path) {
      break;
    }
    LOG(WARNING) << "Could not encrypt/decrypt path " << rel_path << " " << dec_rel_path;
  }
  if (i == kNumEncryptAttempts) {
    CHECK(false) << "Could not encrypt path " << rel_path;
  }
  CHECK(success);

  // Encrypt the data.
  success = EncryptInternal(raw_input, users, &(package->payload.data),
                            &(package->payload.user_enc_session));
  package->payload.data_sha1 = SHA1Hex(package->payload.data);

  // string dec_raw_input;
  // CHECK(Decrypt(package->payload.data,
  //               package->payload.user_enc_session,
  //               &dec_raw_input));
  // CHECK(raw_input == dec_raw_input);

  CHECK(success);
  return true;
}

bool Encryptor::EncryptInternal(
    const string& raw_input, const vector<string>& emails,
    string* data, map<string, string>* user_enc_session) {
  CHECK(data);
  CHECK(user_enc_session);

  string password;
  // Encrypt the file with the session ke.
  char password_bytes[22];

  int attempts = 0;
  for (attempts = 0; attempts < kNumEncryptAttempts; attempts++) {
    memset(password_bytes, '\0', 22);
    crypto::RandBytes(password_bytes, 21);
    password.clear();
    password.assign(password_bytes, 21);

    string compressed_input;
    Gzip::Compress(raw_input, &compressed_input);

    // Cipher the main payload data with the symmetric key algo.
    BlockCipher block_cipher;
    data->clear();
    CHECK(block_cipher.Encrypt(compressed_input, password, data));

    string dec_data;
    CHECK(block_cipher.Decrypt(*data, password, &dec_data));
    string decompressed;
    Gzip::Decompress(dec_data, &decompressed);
    if (decompressed == raw_input) {
      break;
    }
    LOG(ERROR) << "Decrypt check failed.";
  }
  CHECK(attempts < kNumEncryptAttempts);

  // Encrypt the session key per user using RSA.
  DBManagerClient::Options email_key_options;
  email_key_options.type = ClientDB::EMAIL_KEY;
  for (const string& email : emails) {
    string key;
    dbm_->Get(email_key_options, email, &key);
    if (key.empty()) {
      PublicKey pub;
      client_->Exec<void, PublicKey&, const string&>(
          &LockboxServiceClient::GetKeyFromEmail, pub, email);
      key = pub.key;
    }
    CHECK(!key.empty()) << "Could not find user's key anywhere " << email;
    dbm_->Put(email_key_options, email, key);

    string enc_session;
    RSAPEM rsa_pem;
    rsa_pem.PublicEncrypt(key, password, &enc_session);

    user_enc_session->insert(std::make_pair(email, enc_session));
  }

  return true;
}

bool Encryptor::Decrypt(const string& data,
                        const map<string, string>& user_enc_session,
                        string* output) {
  CHECK(output);
  string encrypted_key = user_enc_session.find(user_auth_->email)->second;
  CHECK(!encrypted_key.empty());

  DBManagerClient::Options client_data_options;
  client_data_options.type = ClientDB::CLIENT_DATA;

  string priv_key;
  dbm_->Get(client_data_options, "PRIV_KEY", &priv_key);
  CHECK(!priv_key.empty());

  RSAPEM rsa_pem;
  string out;
  rsa_pem.PrivateDecrypt(user_auth_->password, priv_key, encrypted_key, &out);
  CHECK(!out.empty());

  BlockCipher block_cipher;
  string compressed;
  CHECK(block_cipher.Decrypt(data, out, &compressed));

  Gzip::Decompress(compressed, output);

  return true;
}

bool Encryptor::HybridDecrypt(const HybridCrypto& hybrid,
                              string* output) {
  return Decrypt(hybrid.data, hybrid.user_enc_session, output);
}

} // namespace lockbox
