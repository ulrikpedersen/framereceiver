/*
 * FileWriter.h
 *
 */

#ifndef TOOLS_FILEWRITER_FILEWRITER_H_
#define TOOLS_FILEWRITER_FILEWRITER_H_

#include <string>
#include <vector>
#include <map>

#include <boost/shared_ptr.hpp>

#include <log4cxx/logger.h>
#include <hdf5.h>

using namespace log4cxx;

#include "FileWriterPlugin.h"
#include "ClassLoader.h"

namespace filewriter
{

// Forward declarations
class Frame;


class FileWriter : public filewriter::FileWriterPlugin {
public:
    enum PixelType { pixel_raw_8bit, pixel_raw_16bit, pixel_float32 };
    struct DatasetDefinition {
        std::string name;
        FileWriter::PixelType pixel;
        size_t num_frames;
        std::vector<long long unsigned int> frame_dimensions;
        std::vector<long long unsigned int> chunks;
    };

    struct HDF5Dataset_t {
        hid_t datasetid;
        std::vector<hsize_t> dataset_dimensions;
        std::vector<hsize_t> dataset_offsets;
    };

    explicit FileWriter();
    FileWriter(size_t num_processes, size_t process_rank);
    virtual ~FileWriter();

    void createFile(std::string filename, size_t chunk_align=1024 * 1024);
    void createDataset(const FileWriter::DatasetDefinition & definition);
    void writeFrame(const Frame& frame);
    void writeSubFrames(const Frame& frame);
    void closeFile();

    size_t getFrameOffset(size_t frame_no) const;
    void setStartFrameOffset(size_t frame_no);

    void startWriting();
    void stopWriting();
    FrameReceiver::IpcMessage& configure(FrameReceiver::IpcMessage& config);

private:
    FileWriter(const FileWriter& src); // prevent copying one of these
    hid_t pixelToHdfType(FileWriter::PixelType pixel) const;
    HDF5Dataset_t& get_hdf5_dataset(const std::string dset_name);
    void extend_dataset(FileWriter::HDF5Dataset_t& dset, size_t frame_no) const;
    size_t adjustFrameOffset(size_t frame_no) const;

    void processFrame(boost::shared_ptr<Frame> frame);

    /** Pointer to logger */
    LoggerPtr log_;
    /** Is this plugin writing frames to file? */
    bool writing_;
    /** Number of frames to write to file */
    size_t framesToWrite_;
    /** Number of frames that have been written to file */
    size_t framesWritten_;
    /** Number of sub-frames that have been written to file */
    size_t subFramesWritten_;
    /** Path of the file to write to */
    std::string filePath_;
    /** Name of the file to write to */
    std::string fileName_;
    /** Number of concurrent file writers executing */
    const hsize_t concurrent_processes_;
    /** Rank of this file writer */
    const hsize_t concurrent_rank_;
    /** Starting frame offset */
    hsize_t start_frame_offset_;
    /** Internal ID of the file being written to */
    hid_t hdf5_fileid_;
    /** Map of datasets that are being written to */
    std::map<std::string, FileWriter::HDF5Dataset_t> hdf5_datasets_;
};

/**
 * Registration of this plugin through the ClassLoader.  This macro
 * registers the class without needing to worry about name mangling
 */
REGISTER(FileWriterPlugin, FileWriter, "FileWriter");

}
#endif /* TOOLS_FILEWRITER_FILEWRITER_H_ */
