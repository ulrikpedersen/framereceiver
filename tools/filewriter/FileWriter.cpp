/*
 * FileWriter.cpp
 *
 */
#include <assert.h>

#include "FileWriter.h"
#include <hdf5_hl.h>
#include "Frame.h"

namespace filewriter
{

const std::string FileWriter::CONFIG_PROCESS        = "process";
const std::string FileWriter::CONFIG_PROCESS_NUMBER = "number";
const std::string FileWriter::CONFIG_PROCESS_RANK   = "rank";

const std::string FileWriter::CONFIG_FILE           = "file";
const std::string FileWriter::CONFIG_FILE_NAME      = "name";
const std::string FileWriter::CONFIG_FILE_PATH      = "path";

const std::string FileWriter::CONFIG_DATASET        = "dataset";
const std::string FileWriter::CONFIG_DATASET_CMD    = "cmd";
const std::string FileWriter::CONFIG_DATASET_NAME   = "name";
const std::string FileWriter::CONFIG_DATASET_TYPE   = "datatype";
const std::string FileWriter::CONFIG_DATASET_DIMS   = "dims";
const std::string FileWriter::CONFIG_DATASET_CHUNKS = "chunks";

/**
 * Create a FileWriterPlugin with default values.
 * File path is set to default of current directory, and the
 * filename is set to a default.  The framesToWrite_ member
 * variable is set to a default of 3 (TODO: Change this).
 *
 * The writer plugin is also configured to be a single
 * process writer (no other expected writers) with an offset
 * of 0.
 */
FileWriter::FileWriter() :
  writing_(false),
  masterFrame_(""),
  framesToWrite_(3),
  framesWritten_(0),
  filePath_("./"),
  fileName_("test_file.h5"),
  concurrent_processes_(1),
  concurrent_rank_(0),
  hdf5_fileid_(0),
  start_frame_offset_(0)
{
    this->log_ = Logger::getLogger("FW.FileWriter");
    this->log_->setLevel(Level::getTrace());
    LOG4CXX_TRACE(log_, "FileWriter constructor.");

    this->hdf5_fileid_ = 0;
    this->start_frame_offset_ = 0;
}

/**
 * Destructor.
 */
FileWriter::~FileWriter()
{
    if (this->hdf5_fileid_ != 0) {
        LOG4CXX_TRACE(log_, "destructor closing file");
        H5Fclose(this->hdf5_fileid_);
        this->hdf5_fileid_ = 0;
    }
}

/**
 * Create the HDF5 ready for writing datasets.
 * Currently the file is created with the following:
 * Chunk boundary alignment is set to 4MB.
 * Using the latest library format
 * Created with SWMR access
 * chunk_align parameter not currently used
 *
 * \param[in] filename - Full file name of the file to create.
 * \param[in] chunk_align - Not currently used.
 */
void FileWriter::createFile(std::string filename, size_t chunk_align)
{
    hid_t fapl; // File access property list
    hid_t fcpl;

    // Create file access property list
    fapl = H5Pcreate(H5P_FILE_ACCESS);
    assert(fapl >= 0);

    assert(H5Pset_fclose_degree(fapl, H5F_CLOSE_STRONG) >= 0);

    // Set chunk boundary alignment to 4MB
    assert( H5Pset_alignment( fapl, 65536, 4*1024*1024 ) >= 0);

    // Set to use the latest library format
    assert(H5Pset_libver_bounds(fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST) >= 0);

    // Create file creation property list
    if ((fcpl = H5Pcreate(H5P_FILE_CREATE)) < 0)
    assert(fcpl >= 0);

    // Creating the file with SWMR write access
    LOG4CXX_INFO(log_, "Creating file: " << filename);
    unsigned int flags = H5F_ACC_TRUNC;
    this->hdf5_fileid_ = H5Fcreate(filename.c_str(), flags, fcpl, fapl);
    assert(this->hdf5_fileid_ >= 0);

    // Close file access property list
    assert(H5Pclose(fapl) >= 0);
}

/**
 * Write a frame to the file.
 *
 * \param[in] frame - Reference to the frame.
 */
void FileWriter::writeFrame(const Frame& frame) {
    herr_t status;
    hsize_t frame_no = frame.get_frame_number();

    HDF5Dataset_t& dset = this->get_hdf5_dataset(frame.get_dataset_name());

    hsize_t frame_offset = 0;
    frame_offset = this->getFrameOffset(frame_no);
    this->extend_dataset(dset, frame_offset + 1);

    LOG4CXX_DEBUG(log_, "Writing frame offset=" << frame_no  << " (" << frame_offset << ")"
    		             << " dset=" << frame.get_dataset_name());

    // Set the offset
    std::vector<hsize_t>offset(dset.dataset_dimensions.size());
    offset[0] = frame_offset;

    uint32_t filter_mask = 0x0;
    status = H5DOwrite_chunk(dset.datasetid, H5P_DEFAULT,
                             filter_mask, &offset.front(),
                             frame.get_data_size(), frame.get_data());
    assert(status >= 0);
}

/**
 * Write horizontal subframes direct to dataset chunk.
 *
 * \param[in] frame - Reference to a frame object containing the subframe.
 */
void FileWriter::writeSubFrames(const Frame& frame) {
    herr_t status;
    uint32_t filter_mask = 0x0;
    hsize_t frame_no = frame.get_frame_number();

    HDF5Dataset_t& dset = this->get_hdf5_dataset(frame.get_dataset_name());

    hsize_t frame_offset = 0;
    frame_offset = this->getFrameOffset(frame_no);

    this->extend_dataset(dset, frame_offset + 1);

    LOG4CXX_DEBUG(log_, "Writing frame=" << frame_no << " (" << frame_offset << ")"
    					<< " dset=" << frame.get_dataset_name());

    // Set the offset
    std::vector<hsize_t>offset(dset.dataset_dimensions.size(), 0);
    offset[0] = frame_offset;

    for (int i = 0; i < frame.get_parameter("subframe_count"); i++)
    {
      offset[2] = i * frame.get_dimensions("subframe")[1]; // For P2M: subframe is 704 pixels
        LOG4CXX_DEBUG(log_, "    offset=" << offset[0]
                  << "," << offset[1] << "," << offset[2]);

        LOG4CXX_DEBUG(log_, "    subframe_size=" << frame.get_parameter("subframe_size"));

        status = H5DOwrite_chunk(dset.datasetid, H5P_DEFAULT,
                                 filter_mask, &offset.front(),
                                 frame.get_parameter("subframe_size"),
                                 (static_cast<const char*>(frame.get_data())+(i*frame.get_parameter("subframe_size"))));
        assert(status >= 0);
    }
}

/**
 * Create a HDF5 dataset from the DatasetDefinition.
 *
 * \param[in] definition - Reference to the DatasetDefinition.
 */
void FileWriter::createDataset(const FileWriter::DatasetDefinition& definition)
{
    // Handles all at the top so we can remember to close them
    hid_t dataspace = 0;
    hid_t prop = 0;
    hid_t dapl = 0;
    herr_t status;
    hid_t dtype = pixelToHdfType(definition.pixel);

    std::vector<hsize_t> frame_dims = definition.frame_dimensions;

    // Dataset dims: {1, <image size Y>, <image size X>}
    std::vector<hsize_t> dset_dims(1,1);
    dset_dims.insert(dset_dims.end(), frame_dims.begin(), frame_dims.end());

    // If chunking has not been defined it defaults to a single full frame
    std::vector<hsize_t> chunk_dims(1, 1);
    if (definition.chunks.size() != dset_dims.size()) {
    	chunk_dims = dset_dims;
    } else {
    	chunk_dims = definition.chunks;
    }

    std::vector<hsize_t> max_dims = dset_dims;
    max_dims[0] = H5S_UNLIMITED;

    /* Create the dataspace with the given dimensions - and max dimensions */
    dataspace = H5Screate_simple(dset_dims.size(), &dset_dims.front(), &max_dims.front());
    assert(dataspace >= 0);

    /* Enable chunking  */
    LOG4CXX_DEBUG(log_, "Chunking=" << chunk_dims[0] << ","
                       << chunk_dims[1] << ","
                       << chunk_dims[2]);
    prop = H5Pcreate(H5P_DATASET_CREATE);
    status = H5Pset_chunk(prop, dset_dims.size(), &chunk_dims.front());
    assert(status >= 0);

    char fill_value[8] = {0,0,0,0,0,0,0,0};
    status = H5Pset_fill_value(prop, dtype, fill_value);
    assert(status >= 0);

    dapl = H5Pcreate(H5P_DATASET_ACCESS);

    /* Create dataset  */
    LOG4CXX_DEBUG(log_, "Creating dataset: " << definition.name);
    FileWriter::HDF5Dataset_t dset;
    dset.datasetid = H5Dcreate2(this->hdf5_fileid_, definition.name.c_str(),
                                        dtype, dataspace,
                                        H5P_DEFAULT, prop, dapl);
    assert(dset.datasetid >= 0);
    dset.dataset_dimensions = dset_dims;
    dset.dataset_offsets = std::vector<hsize_t>(3);
    this->hdf5_datasets_[definition.name] = dset;

    LOG4CXX_DEBUG(log_, "Closing intermediate open HDF objects");
    assert( H5Pclose(prop) >= 0);
    assert( H5Pclose(dapl) >= 0);
    assert( H5Sclose(dataspace) >= 0);
}

/**
 * Close the currently open HDF5 file.
 */
void FileWriter::closeFile() {
    LOG4CXX_TRACE(log_, "FileWriter closeFile");
    if (this->hdf5_fileid_ >= 0) {
        assert(H5Fclose(this->hdf5_fileid_) >= 0);
        this->hdf5_fileid_ = 0;
    }
}

/**
 * Convert from a PixelType type to the corresponding HDF5 type.
 *
 * \param[in] pixel - The PixelType type to convert.
 * \return - the equivalent HDF5 type.
 */
hid_t FileWriter::pixelToHdfType(FileWriter::PixelType pixel) const {
    hid_t dtype = 0;
    switch(pixel)
    {
    case pixel_float32:
        dtype = H5T_NATIVE_UINT32;
        break;
    case pixel_raw_16bit:
        dtype = H5T_NATIVE_UINT16;
        break;
    case pixel_raw_8bit:
        dtype = H5T_NATIVE_UINT8;
        break;
    default:
        dtype = H5T_NATIVE_UINT16;
    }
    return dtype;
}

/**
 * Get a HDF5Dataset_t definition by its name.
 *
 * The private map of HDF5 dataset definitions is searched and i found
 * the HDF5Dataset_t definition is returned.  Throws a runtime error if
 * the dataset cannot be found.
 *
 * \param[in] dset_name - name of the dataset to search for.
 * \return - the dataset definition if found.
 */
FileWriter::HDF5Dataset_t& FileWriter::get_hdf5_dataset(const std::string dset_name) {
    // Check if the frame destination dataset has been created
    if (this->hdf5_datasets_.find(dset_name) == this->hdf5_datasets_.end())
    {
        // no dataset of this name exist
        LOG4CXX_ERROR(log_, "Attempted to access non-existent dataset: \"" << dset_name << "\"\n");
        throw std::runtime_error("Attempted to access non-existent dataset");
    }
    return this->hdf5_datasets_.at(dset_name);
}

size_t FileWriter::getFrameOffset(size_t frame_no) const {
    size_t frame_offset = this->adjustFrameOffset(frame_no);

    if (this->concurrent_processes_ > 1) {
        // Check whether this frame should really be in this process
        // Note: this expects the frame numbering from HW/FW to start at 1, not 0!
        if ( (((frame_no-1) % this->concurrent_processes_) - this->concurrent_rank_) != 0) {
            LOG4CXX_WARN(log_, "Unexpected frame: " << frame_no
                                << " in this process rank: "
                                << this->concurrent_rank_);
            throw std::runtime_error("Unexpected frame in this process rank");
        }

        // Calculate the new offset based on how many concurrent processes are running
        frame_offset = frame_offset / this->concurrent_processes_;
    }
    return frame_offset;
}

/** Adjust the incoming frame number with an offset
 *
 * This is a hacky work-around a missing feature in the Mezzanine
 * firmware: the frame number is never reset and is ever incrementing.
 * The file writer can deal with it, by inserting the frame right at
 * the end of a very large dataset (fortunately sparsely written to disk).
 *
 * This function latches the first frame number and subtracts this number
 * from every incoming frame.
 *
 * Throws a std::range_error if a frame is received which has a smaller
 *   frame number than the initial frame used to set the offset.
 *
 * Returns the dataset offset for frame number <frame_no>
 */
size_t FileWriter::adjustFrameOffset(size_t frame_no) const {
    size_t frame_offset = 0;
    if (frame_no < this->start_frame_offset_) {
        // Deal with a frame arriving after the very first frame
        // which was used to set the offset: throw a range error
        throw std::range_error("Frame out of order at start causing negative file offset");
    }

    // Normal case: apply offset
    frame_offset = frame_no - this->start_frame_offset_;
    return frame_offset;
}

/** Part of big nasty work-around for the missing frame counter reset in FW
 *
 */
void FileWriter::setStartFrameOffset(size_t frame_no) {
    this->start_frame_offset_ = frame_no;
}

void FileWriter::extend_dataset(HDF5Dataset_t& dset, size_t frame_no) const {
	herr_t status;
    if (frame_no > dset.dataset_dimensions[0]) {
        // Extend the dataset
        LOG4CXX_DEBUG(log_, "Extending dataset_dimensions[0] = " << frame_no);
        dset.dataset_dimensions[0] = frame_no;
        status = H5Dset_extent( dset.datasetid,
                                &dset.dataset_dimensions.front());
        assert(status >= 0);
    }
}

void FileWriter::processFrame(boost::shared_ptr<Frame> frame)
{
  if (writing_){

    // Check if the frame has defined subframes
    if (frame->has_parameter("subframe_count")){
      // The frame has subframes so write them out
      this->writeSubFrames(*frame);
    } else {
      // The frame has no subframes so write the whole frame
      this->writeFrame(*frame);
    }

    // Check if this is a master frame (for multi dataset acquisitions)
    // of if no master frame has been defined.  If either of these conditions
    // are true then increment the number of frames written.
    if (masterFrame_ == "" || masterFrame_ == frame->get_dataset_name()){
      framesWritten_++;
    }

    // Check if we have written enough frames and stop
    if (framesWritten_ == framesToWrite_){
      this->stopWriting();
    }
  }
}

void FileWriter::startWriting()
{
  if (!writing_){
    // Create the file
    this->createFile(filePath_ + fileName_);

    // Create the datasets from the definitions
    std::map<std::string, FileWriter::DatasetDefinition>::iterator iter;
    for (iter = this->dataset_defs_.begin(); iter != this->dataset_defs_.end(); ++iter){
      FileWriter::DatasetDefinition dset_def = iter->second;
      dset_def.num_frames = framesToWrite_;
      this->createDataset(dset_def);
    }

    // Reset counters
    framesWritten_ = 0;

    // Set writing flag to true
    writing_ = true;
  }
}

void FileWriter::stopWriting()
{
  if (writing_){
    writing_ = false;
    this->closeFile();
  }
}

void FileWriter::configure(FrameReceiver::IpcMessage& config, FrameReceiver::IpcMessage& reply)
{
  LOG4CXX_DEBUG(log_, config.encode());

  // Check to see if we are configuring the process number and rank
  if (config.has_param(FileWriter::CONFIG_PROCESS)){
    FrameReceiver::IpcMessage processConfig(config.get_param<const rapidjson::Value&>(FileWriter::CONFIG_PROCESS));
    this->configureProcess(processConfig, reply);
  }

  // Check to see if we are configuring the file path and name
  if (config.has_param(FileWriter::CONFIG_FILE)){
    FrameReceiver::IpcMessage fileConfig(config.get_param<const rapidjson::Value&>(FileWriter::CONFIG_FILE));
    this->configureFile(fileConfig, reply);
  }

  // Check to see if we are configuring a dataset
  if (config.has_param(FileWriter::CONFIG_DATASET)){
    FrameReceiver::IpcMessage dsetConfig(config.get_param<const rapidjson::Value&>(FileWriter::CONFIG_DATASET));
    this->configureDataset(dsetConfig, reply);
  }

  if (config.has_param("frames")){
    framesToWrite_ = config.get_param<int>("frames");
  }
  // Final check is to start or stop writing
  if (config.has_param("write")){
    if (config.get_param<bool>("write") == true){
      this->startWriting();
    } else {
      this->stopWriting();
    }
  }
}

void FileWriter::configureProcess(FrameReceiver::IpcMessage& config, FrameReceiver::IpcMessage& reply)
{
  // If we are writing a file then we cannot change these items
  if (this->writing_){
    LOG4CXX_ERROR(log_, "Cannot change concurrent processes or rank whilst writing");
    throw std::runtime_error("Cannot change concurrent processes or rank whilst writing");
  }

  // Check for process number and rank number
  if (config.has_param(FileWriter::CONFIG_PROCESS_NUMBER)){
    this->concurrent_processes_ = config.get_param<int>(FileWriter::CONFIG_PROCESS_NUMBER);
    LOG4CXX_DEBUG(log_, "Concurrent processes changed to " << this->concurrent_processes_);
  }
  if (config.has_param(FileWriter::CONFIG_PROCESS_RANK)){
    this->concurrent_rank_ = config.get_param<int>(FileWriter::CONFIG_PROCESS_RANK);
    LOG4CXX_DEBUG(log_, "Process rank changed to " << this->concurrent_rank_);
  }
}

void FileWriter::configureFile(FrameReceiver::IpcMessage& config, FrameReceiver::IpcMessage& reply)
{
  // If we are writing a file then we cannot change these items
  if (this->writing_){
    LOG4CXX_ERROR(log_, "Cannot change file path or name whilst writing");
    throw std::runtime_error("Cannot change file path or name whilst writing");
  }

  LOG4CXX_DEBUG(log_, "Configure file name and path");
  // Check for file path and file name
  if (config.has_param(FileWriter::CONFIG_FILE_PATH)){
    this->filePath_ = config.get_param<std::string>(FileWriter::CONFIG_FILE_PATH);
    LOG4CXX_DEBUG(log_, "File path changed to " << this->filePath_);
  }
  if (config.has_param(FileWriter::CONFIG_FILE_NAME)){
    this->fileName_ = config.get_param<std::string>(FileWriter::CONFIG_FILE_NAME);
    LOG4CXX_DEBUG(log_, "File name changed to " << this->fileName_);
  }
}

void FileWriter::configureDataset(FrameReceiver::IpcMessage& config, FrameReceiver::IpcMessage& reply)
{
  // If we are writing a file then we cannot change these items
  if (this->writing_){
    LOG4CXX_ERROR(log_, "Cannot update datasets whilst writing");
    throw std::runtime_error("Cannot update datasets whilst writing");
  }

  LOG4CXX_DEBUG(log_, "Configure dataset");
  // Read the dataset command
  if (config.has_param(FileWriter::CONFIG_DATASET_CMD)){
    std::string cmd = config.get_param<std::string>(FileWriter::CONFIG_DATASET_CMD);

    // Command for creating a new dataset description
    if (cmd == "create"){
      FileWriter::DatasetDefinition dset_def;
      // There must be a name present for the dataset
      if (config.has_param(FileWriter::CONFIG_DATASET_NAME)){
        dset_def.name = config.get_param<std::string>(FileWriter::CONFIG_DATASET_NAME);
      } else {
        LOG4CXX_ERROR(log_, "Cannot create a dataset without a name");
        throw std::runtime_error("Cannot create a dataset without a name");
      }

      // There must be a type present for the dataset
      if (config.has_param(FileWriter::CONFIG_DATASET_TYPE)){
        dset_def.pixel = (filewriter::FileWriter::PixelType)config.get_param<int>(FileWriter::CONFIG_DATASET_TYPE);
      } else {
        LOG4CXX_ERROR(log_, "Cannot create a dataset without a data type");
        throw std::runtime_error("Cannot create a dataset without a data type");
      }

      // There must be dimensions present for the dataset
      if (config.has_param(FileWriter::CONFIG_DATASET_DIMS)){
        const rapidjson::Value& val = config.get_param<const rapidjson::Value&>(FileWriter::CONFIG_DATASET_DIMS);
        // Loop over the dimension values
        dimensions_t dims(val.Size());
        for (rapidjson::SizeType i = 0; i < val.Size(); i++){
          const rapidjson::Value& dim = val[i];
          dims[i] = dim.GetUint64();
        }
        dset_def.frame_dimensions = dims;
      } else {
        LOG4CXX_ERROR(log_, "Cannot create a dataset without dimensions");
        throw std::runtime_error("Cannot create a dataset without dimensions");
      }

      // There might be chunking dimensions present for the dataset, this is not required
      if (config.has_param(FileWriter::CONFIG_DATASET_CHUNKS)){
        const rapidjson::Value& val = config.get_param<const rapidjson::Value&>(FileWriter::CONFIG_DATASET_CHUNKS);
        // Loop over the dimension values
        dimensions_t chunks(val.Size());
        for (rapidjson::SizeType i = 0; i < val.Size(); i++){
          const rapidjson::Value& dim = val[i];
          chunks[i] = dim.GetUint64();
        }
        dset_def.chunks = chunks;
      }

      LOG4CXX_DEBUG(log_, "Creating dataset [" << dset_def.name << "] (" << dset_def.frame_dimensions[0] << ", " << dset_def.frame_dimensions[1] << ")");
      // Add the dataset definition to the store
      this->dataset_defs_[dset_def.name] = dset_def;
    }
  }
}

void FileWriter::status(FrameReceiver::IpcMessage& status)
{
  // Record the plugin's status items
  LOG4CXX_DEBUG(log_, "File name " << this->fileName_);

  status.set_param(getName() + "/writing", this->writing_);
  status.set_param(getName() + "/frames_max", this->framesToWrite_);
  status.set_param(getName() + "/frames_written", this->framesWritten_);
  status.set_param(getName() + "/file_path", this->filePath_);
  status.set_param(getName() + "/file_name", this->fileName_);
  status.set_param(getName() + "/processes", this->concurrent_processes_);
  status.set_param(getName() + "/rank", this->concurrent_rank_);

  // Check for datasets
  std::map<std::string, FileWriter::DatasetDefinition>::iterator iter;
  for (iter = this->dataset_defs_.begin(); iter != this->dataset_defs_.end(); ++iter){
    // Add the dataset type
    status.set_param(getName() + "/datasets/" + iter->first + "/type", (int)iter->second.pixel);

    // Check for and add dimensions
    if (iter->second.frame_dimensions.size() > 0){
      std::string dimParamName = getName() + "/datasets/" + iter->first + "/dimensions[]";
      for (int index = 0; index < iter->second.frame_dimensions.size(); index++){
        status.set_param(dimParamName, (uint64_t)iter->second.frame_dimensions[index]);
      }
    }
    // Check for and add chunking dimensions
    if (iter->second.chunks.size() > 0){
      std::string chunkParamName = getName() + "/datasets/" + iter->first + "/chunks[]";
      for (int index = 0; index < iter->second.chunks.size(); index++){
        status.set_param(chunkParamName, (uint64_t)iter->second.chunks[index]);
      }
    }
  }

  /*
  status.AddMember("writing", rapidjson::Value().SetObject().SetBool(this->writing_), allocator);
  status.AddMember("frames_max", rapidjson::Value().SetObject().SetInt(this->framesToWrite_), allocator);
  status.AddMember("frames_written", rapidjson::Value().SetObject().SetInt(this->framesWritten_), allocator);
  status.AddMember("file_path", rapidjson::Value().SetObject().SetString(this->filePath_.c_str(), allocator), allocator);
  status.AddMember("file_name", rapidjson::Value().SetObject().SetString(this->fileName_.c_str(), allocator), allocator);
  status.AddMember("processes", rapidjson::Value().SetObject().SetInt(this->concurrent_processes_), allocator);
  status.AddMember("rank", rapidjson::Value().SetObject().SetInt(this->concurrent_rank_), allocator);

  // Check for datasets
  rapidjson::Value dsetdefs;
  dsetdefs.SetObject();
  std::map<std::string, FileWriter::DatasetDefinition>::iterator iter;
  for (iter = this->dataset_defs_.begin(); iter != this->dataset_defs_.end(); ++iter){
    // Create the dataset value
    rapidjson::Value dset;
    dset.SetObject();
    // Add the dataset type
    dset.AddMember("type", rapidjson::Value().SetObject().SetInt(iter->second.pixel), allocator);
    // Check for and add dimensions
    if (iter->second.frame_dimensions.size() > 0){
      rapidjson::Value dims;
      dims.SetObject().SetArray();
      for (int index = 0; index < iter->second.frame_dimensions.size(); index++){
        dims.PushBack<uint64_t>(iter->second.frame_dimensions[index], allocator);
      }
      dset.AddMember("dimensions", dims, allocator);
    }
    // Check for and add chunking dimensions
    if (iter->second.chunks.size() > 0){
      rapidjson::Value chunks;
      chunks.SetObject().SetArray();
      for (int index = 0; index < iter->second.chunks.size(); index++){
        chunks.PushBack<uint64_t>(iter->second.chunks[index], allocator);
      }
      dset.AddMember("chunks", chunks, allocator);
    }
    // Now add the dataset to the definitions
    dsetdefs.AddMember(rapidjson::Value().SetObject().SetString(iter->first.c_str(), allocator), dset, allocator);
  }
  // Finally add the dataset defs to the status
  status.AddMember("datasets", dsetdefs, allocator);
  */
}

}
