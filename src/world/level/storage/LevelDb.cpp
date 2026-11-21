#include "world/level/storage/LevelDb.hpp"

#include <mutex>

#if defined(PERLITE_HAS_LEVELDB)
#include <leveldb/cache.h>
#include <leveldb/db.h>
#include <leveldb/filter_policy.h>
#include <leveldb/options.h>
#include <leveldb/write_batch.h>
#endif

namespace perlite {

#if defined(PERLITE_HAS_LEVELDB)
    struct LevelDb::Impl {
        leveldb::DB* db = nullptr;
        leveldb::Options options;
        const leveldb::FilterPolicy* filter = nullptr;
        leveldb::Cache* cache = nullptr;
        leveldb::WriteBatch batch;
        size_t pending = 0;
        mutable std::mutex batchMutex;

        ~Impl() {
            delete db;
            delete filter;
            delete cache;
        }
    };

    LevelDb::LevelDb() : impl(std::make_unique<Impl>()) {}

    LevelDb::~LevelDb() {
        Close();
    }

    bool LevelDb::Available() {
        return true;
    }

    bool LevelDb::Open(const std::string& path, std::string& error) {
        Close();
        impl = std::make_unique<Impl>();
        impl->filter = leveldb::NewBloomFilterPolicy(10);
        impl->cache = leveldb::NewLRUCache(32 * 1024 * 1024);
        impl->options.create_if_missing = true;
        impl->options.filter_policy = impl->filter;
        impl->options.block_cache = impl->cache;
        impl->options.write_buffer_size = 4 * 1024 * 1024;
        impl->options.block_size = 160 * 1024;
        impl->options.compression = leveldb::kZlibRawCompression;
        const leveldb::Status status = leveldb::DB::Open(impl->options, path, &impl->db);
        if (!status.ok()) {
            error = status.ToString();
            impl->db = nullptr;
            return false;
        }
        return true;
    }

    void LevelDb::Close() {
        if (!impl || !impl->db)
            return;
        Commit();
        delete impl->db;
        impl->db = nullptr;
    }

    bool LevelDb::IsOpen() const {
        return impl && impl->db;
    }

    bool LevelDb::Get(std::string_view key, std::string& value) const {
        if (!IsOpen())
            return false;
        const leveldb::Status s =
            impl->db->Get(leveldb::ReadOptions(), leveldb::Slice(key.data(), key.size()), &value);
        return s.ok();
    }

    void LevelDb::Put(std::string_view key, std::string_view value) {
        if (!IsOpen())
            return;
        std::lock_guard lock(impl->batchMutex);
        impl->batch.Put(leveldb::Slice(key.data(), key.size()), leveldb::Slice(value.data(), value.size()));
        impl->pending++;
    }

    void LevelDb::Delete(std::string_view key) {
        if (!IsOpen())
            return;
        std::lock_guard lock(impl->batchMutex);
        impl->batch.Delete(leveldb::Slice(key.data(), key.size()));
        impl->pending++;
    }

    bool LevelDb::Commit(std::string* error) {
        if (!IsOpen())
            return false;
        std::lock_guard lock(impl->batchMutex);
        if (impl->pending == 0)
            return true;
        const leveldb::Status s = impl->db->Write(leveldb::WriteOptions(), &impl->batch);
        impl->batch.Clear();
        impl->pending = 0;
        if (!s.ok() && error)
            *error = s.ToString();
        return s.ok();
    }

    void LevelDb::ForEachPrefix(std::string_view prefix,
                                const std::function<bool(std::string_view, std::string_view)>& fn) const {
        if (!IsOpen())
            return;
        std::unique_ptr<leveldb::Iterator> it(impl->db->NewIterator(leveldb::ReadOptions()));
        for (it->Seek(leveldb::Slice(prefix.data(), prefix.size())); it->Valid(); it->Next()) {
            const leveldb::Slice k = it->key();
            const std::string_view key(k.data(), k.size());
            if (!key.starts_with(prefix))
                break;
            const leveldb::Slice v = it->value();
            if (!fn(key, std::string_view(v.data(), v.size())))
                break;
        }
    }
#else
    struct LevelDb::Impl {};
    LevelDb::LevelDb() = default;
    LevelDb::~LevelDb() = default;
    bool LevelDb::Available() {
        return false;
    }
    bool LevelDb::Open(const std::string&, std::string& error) {
        error = "this build has no LevelDB support (PERLITE_LEVELDB=OFF)";
        return false;
    }
    void LevelDb::Close() {}
    bool LevelDb::IsOpen() const {
        return false;
    }
    bool LevelDb::Get(std::string_view, std::string&) const {
        return false;
    }
    void LevelDb::Put(std::string_view, std::string_view) {}
    void LevelDb::Delete(std::string_view) {}
    bool LevelDb::Commit(std::string*) {
        return false;
    }
    void LevelDb::ForEachPrefix(std::string_view,
                                const std::function<bool(std::string_view, std::string_view)>&) const {}
#endif
}
