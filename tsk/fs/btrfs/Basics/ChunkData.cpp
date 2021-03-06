//! \file
//! \author Shujian Yang
//!
//! Implementation of class ChunkData

#include <sstream>
#include "ChunkData.h"
#include "../../../utils/ReadInt.h"

namespace btrForensics{

    //! Constructor of ChunkData.
    //!
    //! \param endian The endianess of the array.
    //! \param arr Byte array storing inode data.
    //!
    ChunkData::ChunkData(TSK_ENDIAN_ENUM endian, uint8_t arr[])
    {
        int arIndex(0);

        chunkSize = read64Bit(endian, arr + arIndex);
        arIndex += 0x08;
        objId = read64Bit(endian, arr + arIndex);
        arIndex += 0x08;

        stripeLength = read64Bit(endian, arr + arIndex);
        arIndex += 0x08;
        type = read64Bit(endian, arr + arIndex);
        arIndex += 0x08;

        optIoAlignment = read32Bit(endian, arr + arIndex);
        arIndex += 0x04;
        optIoWidth = read32Bit(endian, arr + arIndex);
        arIndex += 0x04;
        minIoSize = read32Bit(endian, arr + arIndex);
        arIndex += 0x04;

        numStripe = read16Bit(endian, arr + arIndex);
        arIndex += 0x02;
        subStripe = read16Bit(endian, arr + arIndex);
        arIndex += 0x02;

        //TODO: Check that it is a legitimate stripe
        for(int i=0; i<numStripe; i++) {
            deviceIds.push_back(read64Bit(endian, arr + arIndex));
            arIndex += 0x08;
            offsets.push_back(read64Bit(endian, arr + arIndex));
            arIndex += 0x08;
            deviceUUIDs.push_back(UUID(endian, arr + arIndex));
            arIndex += 0x10;
        }

        bytesUsed = arIndex;
    }

    //! Return infomation about the item data as string.
    std::string ChunkData::dataInfo() const
    {
        std::ostringstream oss;
        oss << "Number of stripes: " << numStripe << '\n';
        for(int i=0; i<deviceIds.size(); i++){
            oss << "DeviceID: " << deviceIds.at(i) << '\n';
            oss << "Offset: 0x" << std::uppercase << std::hex << offsets.at(i) << std::dec << '\n';
            oss << "DeviceUUID:" << deviceUUIDs.at(i).encode() << '\n';
        }
        return oss.str();
    }

}
