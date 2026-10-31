#include "loader/chunkcoordsblocks.h"
#include "loader/chunkcoords.h"
#include "model/mapviewstate.h"
#include "bedrock_level.h"
#include "color.h"
#include "leveldb/db.h"
#include "raw_chunk.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "binary_io.h"

namespace {
    using chunkcoords_blocks::Status;
    int failures = 0;

    void expect(bool condition, const char* description) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", description);
            ++failures;
        }
    }

    void appendString(std::string& out, std::string_view value) {
        bl::binary::append_u16_le(out, static_cast<std::uint16_t>(value.size()));
        out.append(value);
    }

    std::string blockTag(std::string_view name) {
        std::string out(1, '\x0a');
        appendString(out, "");
        out.push_back('\x0a');
        appendString(out, "states");
        out.push_back('\x09');
        appendString(out, "nested_list");
        out.push_back('\x03');
        bl::binary::append_i32_le(out, 2);
        bl::binary::append_i32_le(out, 11);
        bl::binary::append_i32_le(out, 22);
        out.push_back('\x0c');
        appendString(out, "nested_array");
        bl::binary::append_i32_le(out, 1);
        bl::binary::append_u64_le(out, 123);
        out.push_back('\0');
        out.push_back('\x08');
        appendString(out, "name");
        appendString(out, name);
        out.push_back('\x03');
        appendString(out, "version");
        bl::binary::append_i32_le(out, 17879555);
        out.push_back('\0');
        return out;
    }

    std::string subchunk(int version, int section, unsigned bits, int local_y, unsigned selected) {
        std::string out;
        out.push_back(static_cast<char>(version));
        out.push_back(1);
        if (version == 9) out.push_back(static_cast<char>(section));
        out.push_back(static_cast<char>(bits << 1));
        if (bits) {
            const unsigned per_word = 32 / bits;
            std::vector<std::uint32_t> words((4096 + per_word - 1) / per_word, 0);
            const unsigned index = 8 * 256 + 8 * 16 + local_y;
            words[index / per_word] |= selected << ((index % per_word) * bits);
            for (const auto word : words) bl::binary::append_u32_le(out, word);
            bl::binary::append_u32_le(out, 2);
            out += blockTag("minecraft:air");
        }
        out += blockTag("minecraft:grass_block");
        return out;
    }
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    for (const int version : {8, 9}) {
        for (const int section : {-4, -1, 0, 19}) {
            for (const unsigned bits : {0, 1, 2, 3, 4, 5, 6, 8, 16}) {
                const auto value = subchunk(version, section, bits, 7, bits ? 1 : 0);
                const auto sample = chunkcoords_blocks::sampleBlock(value, section * 16 + 7);
                expect(sample.status == Status::Found && sample.name == "minecraft:grass_block" && sample.y == section * 16 + 7,
                       "v8/v9, signed section, uniform and padded packed-word layouts");
                if (bits) {
                    const auto air = chunkcoords_blocks::sampleBlock(value, section * 16 + 6);
                    expect(air.status == Status::Empty && air.name == "minecraft:air", "read selected index rather than palette[0]");
                }
            }
        }
    }

    const auto valid = subchunk(9, -1, 3, 15, 1);
    for (std::size_t length = 0; length < valid.size(); ++length)
        expect(chunkcoords_blocks::sampleBlock(std::string_view(valid).substr(0, length), -1).status == Status::Malformed,
               "truncated packed indices / palette NBT are rejected");
    expect(chunkcoords_blocks::sampleBlock(valid, 16).status == Status::Malformed, "v9 Y index must match the selected section");

    auto invalid = valid;
    invalid[0] = 7;
    expect(chunkcoords_blocks::sampleBlock(invalid, -1).status == Status::Unsupported, "unsupported subchunk version");
    invalid = valid;
    invalid[3] |= 1;
    expect(chunkcoords_blocks::sampleBlock(invalid, -1).status == Status::Unsupported, "runtime palette is reported explicitly");
    invalid = valid;
    invalid[3] = static_cast<char>(17 << 1);
    expect(chunkcoords_blocks::sampleBlock(invalid, -1).status == Status::Malformed, "invalid bit width");
    invalid = subchunk(9, 0, 2, 0, 3);
    expect(chunkcoords_blocks::sampleBlock(invalid, 0).status == Status::Malformed, "out-of-range palette index");
    invalid = valid;
    invalid[1] = 0;
    expect(chunkcoords_blocks::sampleBlock(invalid, -1).status == Status::Empty, "zero storage layers");

    // The second layer is deliberately invalid: this experiment samples only
    // base storage, without paying to decode waterlogging or other overlays.
    invalid = valid;
    invalid[1] = 2;
    invalid.push_back('\xff');
    expect(chunkcoords_blocks::sampleBlock(invalid, -1).status == Status::Found, "only the base layer is decoded");

    {
        QTemporaryDir world;
        expect(world.isValid(), "temporary test world");
        std::string dat;
        bl::binary::append_u32_le(dat, 10);
        bl::binary::append_u32_le(dat, 4);
        dat.append("\x0a\0\0\0", 4);
        QFile file(world.filePath("level.dat"));
        expect(file.open(QIODevice::WriteOnly) && file.write(dat.data(), dat.size()) == qint64(dat.size()), "write fixture level.dat");
        file.close();
        leveldb::Options dbOptions;
        dbOptions.create_if_missing = true;
        leveldb::DB* db = nullptr;
        const auto opened = leveldb::DB::Open(dbOptions, world.filePath("db").toStdString(), &db);
        expect(opened.ok(), "create fixture database");
        if (db) {
            for (const bl::chunk_pos pos : {bl::chunk_pos{-1, -1, 0}, {0, 0, 0}, {128, 0, 0}, {-1, -1, 1}, {-1, -1, 2}}) {
                const int section = pos.dim == 0 && pos.x == -1 ? -4 : 0;
                const int h = section * 16 + 16 - (pos.dim == 0 ? -64 : 0);
                std::string heights;
                for (int i = 0; i < 256; ++i) bl::binary::append_i16_le(heights, h);
                const auto put = [&](bl::chunk_key::key_type type, int y, std::string_view value) {
                    bl::chunk_key key;
                    key.cp = pos; key.type = type; key.y_index = static_cast<int8_t>(y);
                    expect(db->Put({}, key.to_raw(), leveldb::Slice(value.data(), value.size())).ok(), "write fixture key");
                };
                put(bl::chunk_key::Data3D, 0, heights);
                put(bl::chunk_key::VersionOld, 0, "\x28");
                put(bl::chunk_key::SubChunkTerrain, section, subchunk(9, section, 3, 15, 1));
                put(bl::chunk_key::SubChunkTerrain, 3, "malformed unused section");
            }
            delete db;
            bl::bedrock_level level{true, ""};
            expect(level.open(world.path().toStdString()), "open fixture world");
            if (level.db()) {
                bl::init_block_color_from_json(R"({"minecraft:grass_block":{"default":"#709050ff"}})");
                ChunkCoordsIndex index;
                ChunkCoordsLoadOptions options;
                options.build_overview = true;
                options.sampling_queue_bytes = 1;
                ChunkCoordsLoadStats stats;
                const std::atomic_bool stop{false};
                const auto edit = [&](std::uint64_t scanned, std::uint64_t) {
                    if (!scanned) index.updateOverview({-1, -1, 0}, qRgb(220, 10, 20));
                };
                expect(index.load(level, stop, edit, &stats, options), "complete bounded pipeline with scan-time edit");
                expect(stats.unique_chunks == 5 && stats.sampled_blocks == 5 && stats.sampling_malformed == 0,
                       "sample only height-selected sections across interleaved dimensions and late version markers");
                const auto image = index.overviewImage(0, QRect(-128, -128, 256, 256), QSize(256, 256));
                expect(image.pixel(127, 127) == qRgb(220, 10, 20), "scan completion replays edited overview pixels");
                expect(qAlpha(image.pixel(128, 128)) == 255 && qAlpha(image.pixel(129, 129)) == 0,
                       "one texel per chunk with transparent unsampled space");
                expect(qAlpha(index.overviewImage(1, QRect(-1, -1, 1, 1), QSize(1, 1)).pixel(0, 0)) == 255,
                       "dimension-specific overview pages");
                expect(qAlpha(index.overviewImage(0, QRect(2048, 2048, 1, 1), QSize(1, 1)).pixel(0, 0)) == 0,
                       "distant windows do not wrap to existing data");
                const auto regions = index.terrainRegions(0, QRect(-1, -1, 18, 2));
                expect(regions.size() == 3, "sparse region enumeration includes negative and page-boundary chunks");
                bl::raw_chunk empty({-1, -1, 0});
                index.updateOverview(empty);
                expect(qAlpha(index.overviewImage(0, QRect(-1, -1, 1, 1), QSize(1, 1)).pixel(0, 0)) == 0,
                       "clearing a chunk removes its overview sample");
                ChunkCoordsIndex disabled;
                expect(disabled.load(level, stop) &&
                       qAlpha(disabled.overviewImage(0, QRect(0, 0, 1, 1), QSize(1, 1)).pixel(0, 0)) == 0,
                       "disabled sampling produces no overview");
                const std::atomic_bool cancelled{true};
                expect(!disabled.load(level, cancelled, {}, nullptr, options), "cancelled pipeline joins without deadlock");
            }
        }
    }
    {
        MapViewState view;
        view.setViewportSize(QSize(1000, 600));
        view.applyTerrainZoomLimits(false);
        view.setScale(0.001, QPointF(500, 300));
        expect(view.scale() == 0.5, "without overview zoom stops at 32 blocks per texel");
        view.applyTerrainZoomLimits(true);
        view.setScale(0.125, QPointF(500, 300));
        expect(view.scale() == 0.125, "overview allows zooming beyond the terrain boundary");
        view.applyTerrainZoomLimits(false);
        expect(view.scale() == 0.5, "disabling overview clamps an already zoomed-out camera");
    }
    std::printf("chunkcoords raw block decoder: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
