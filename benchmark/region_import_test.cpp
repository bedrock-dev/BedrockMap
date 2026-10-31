#include "controller/mapcontroller.h"
#include "controller/regionimportcontroller.h"
#include "loader/asynclevelloader.h"
#include "loader/exportedregion.h"
#include "binary_io.h"
#include "leveldb/db.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>
#include <limits>

namespace {
    int failures = 0;

    void expect(bool condition, const char* message) {
        if (condition) return;
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }

    bool until(const std::function<bool()>& done) {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < 10000) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        return done();
    }

    QUrl writeRegion(const QTemporaryDir& dir, const QString& name, std::initializer_list<bl::chunk_pos> positions) {
        ExportedRegion region;
        for (const auto& position : positions) {
            bl::raw_chunk chunk(position);
            chunk.set_normal(bl::chunk_key::VersionNew, std::string(1, '\x28'));
            region.addChunk(chunk);
        }
        const auto bytes = region.serialize();
        QFile file(dir.filePath(name));
        expect(file.open(QIODevice::WriteOnly) && file.write(bytes.data(), bytes.size()) == qint64(bytes.size()),
               "write region fixture");
        return QUrl::fromLocalFile(file.fileName());
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir files;
    expect(files.isValid(), "temporary fixture folder");
    const auto source = writeRegion(files, "shape.bchks", {{-3, -2, 0}, {-2, -2, 0}, {-3, -1, 0}});
    RegionImportController placement(nullptr);
    int errors = 0;
    QObject::connect(&placement, &RegionImportController::failed, &app, [&](const QString&) { ++errors; });
    expect(placement.loadFile(source, {10, 20}, 1), "start asynchronous read");
    expect(until([&] { return !placement.loading(); }) && placement.active(), "finish asynchronous read");
    expect(placement.targetX() == 10 && placement.targetZ() == 20 && placement.dimension() == 1,
           "loading preserves initial destination");
    expect(placement.width() == 2 && placement.height() == 2 && placement.chunkCount() == 3,
           "sparse source metadata");
    expect(!placement.beginDrag({11.5, 21.5}), "hole inside bounding box belongs to the map");
    placement.updateDrag({100, 100});
    expect(placement.targetX() == 10 && placement.targetZ() == 20, "hover and outside drags cannot move placement");
    expect(placement.beginDrag({10.25, 20.25}), "begin drag inside imported chunks");
    placement.updateDrag({12.9, 19.1});
    expect(placement.targetX() == 13 && placement.targetZ() == 19, "drag snaps both axes to nearest chunk");
    placement.endDrag();
    placement.updateDrag({100, 100});
    expect(placement.targetX() == 13 && placement.targetZ() == 19, "ending drag freezes placement");
    expect(placement.source()->chunks().front().pos() == bl::chunk_pos(-3, -2, 0), "placement never mutates source data");
    placement.setTargetX(std::numeric_limits<int>::max());
    const auto offset = placement.offset();
    for (const auto& chunk : placement.source()->chunks()) {
        const qint64 x = qint64(chunk.pos().x) + offset.x();
        expect(x * 16 + 15 <= std::numeric_limits<int>::max(), "destination fits signed block coordinates");
    }
    expect(qint64(offset.x()) * 16 <= std::numeric_limits<int>::max(), "translation fits raw_chunk move_to offset");
    placement.cancel();
    expect(!placement.active() && placement.region().isEmpty(), "cancel clears preview");

    expect(placement.loadFile(source, {-10, -20}, 0), "start read for cancellation");
    placement.cancel();
    expect(until([&] { return !placement.loading(); }) && !placement.active(), "late read result cannot restore canceled preview");
    const auto duplicate = writeRegion(files, "duplicate.bchks", {{0, 0, 0}, {0, 0, 0}});
    placement.loadFile(duplicate, {}, 0);
    expect(until([&] { return !placement.loading(); }) && !placement.active() && errors == 1, "reject duplicate chunks");
    const auto mixed = writeRegion(files, "mixed.bchks", {{0, 0, 0}, {1, 0, 1}});
    placement.loadFile(mixed, {}, 0);
    expect(until([&] { return !placement.loading(); }) && !placement.active() && errors == 2, "reject mixed dimensions");
    placement.loadFile(QUrl::fromLocalFile(files.filePath("missing.bchks")), {}, 0);
    expect(until([&] { return !placement.loading(); }) && errors == 3, "report missing file");

    QTemporaryDir world;
    std::string dat;
    bl::binary::append_u32_le(dat, 10);
    bl::binary::append_u32_le(dat, 4);
    dat.append("\x0a\0\0\0", 4);
    QFile levelDat(world.filePath("level.dat"));
    expect(levelDat.open(QIODevice::WriteOnly) && levelDat.write(dat.data(), dat.size()) == qint64(dat.size()),
           "write temporary world metadata");
    levelDat.close();
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db = nullptr;
    expect(leveldb::DB::Open(options, world.filePath("db").toStdString(), &db).ok(), "create temporary database");
    delete db;
    AsyncLevelLoader loader;
    loader.setPreloadAllChunkCoords(false);
    expect(loader.open(world.path().toStdString(), ""), "open temporary world");
    if (loader.isOpen()) {
        MapViewState view;
        MapController controller(&view);
        controller.setLevelLoader(&loader);
        expect(controller.beginImportAsync(source, -10, -20), "start import into temporary world");
        expect(until([&] { return !controller.regionImport()->loading(); }), "wait for import preview");
        expect(!loader.isDirty(), "preview does not edit world");
        controller.cancelImport();
        expect(!loader.isDirty(), "canceled preview leaves world clean");
        controller.beginImportAsync(source, -10, -20);
        expect(until([&] { return !controller.regionImport()->loading(); }), "reload canceled source");
        controller.setEditingBlocked(true);
        expect(!controller.confirmImport(), "saving blocks confirmation");
        controller.setEditingBlocked(false);
        expect(controller.confirmImport(), "confirm import");
        expect(until([&] { return !controller.operationRunning(); }), "wait for import operation");
        expect(loader.isDirty() && !controller.regionImport()->active(), "successful import stages changes and clears preview");
        expect(controller.selectionChunkCount() == 3 && view.selection().contains({-10, -20}) &&
               !view.selection().contains({-9, -19}), "imported sparse region becomes selection");
        const auto imported = loader.getRawChunk({-10, -20, 0});
        expect(imported && imported->pos() == bl::chunk_pos(-10, -20, 0), "raw chunk stored at destination");
        controller.setLevelLoader(nullptr);
    }
    loader.close();
    std::printf("region import: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
