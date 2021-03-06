/*
 * BTRFS_POOL.cpp
 * The Sleuth Kit
 *
 * This software is distributed under the Common Public License 1.0
 *
 */

/**
 * \file BTRFS_POOL.cpp
 * Creates a BTRFS_POOL object, subclass of TSK_POOL object
 */

#include <string.h>
#include <cmath>
#include <iomanip>
#include "BTRFS_POOL.h"
#include "../../tsk/utils/ReadInt.h"
#include "../../tsk/utils/Uuid.h"
#include "../../tsk/fs/btrfs/Trees/SuperBlock.h"
#include "../../tsk/fs/btrfs/Trees/Trees.h"
#include "../../tsk/fs/btrfs/Examiners/Examiners.h"

using namespace btrForensics;

using namespace std;

BTRFS_POOL::BTRFS_POOL(TSK_POOL_INFO *pool)
        : initialized(false), no_all_devices(0), no_available_devices(0), pool(pool) {

    std::vector<char> diskData(SuperBlock::SIZE_OF_SPR_BLK);
    SuperBlock *supblk = nullptr;
    BTRFS_DEVICE *dev = nullptr;
    examiner = nullptr;

    for (auto &it : pool->members) {
        //cerr << "DBG: " << it.first << endl;
        tsk_img_read(it.second, SuperBlock::ADDR_OF_SPR_BLK, diskData.data(), SuperBlock::SIZE_OF_SPR_BLK);
        //TODO: Try, catch
        supblk = new SuperBlock(TSK_LIT_ENDIAN, (uint8_t *) diskData.data());

        if (!initialized) {
            no_all_devices = supblk->getNumDevices();
            pool_guid = supblk->getDevData().getFSUUID().encode();
            superblock = new SuperBlock(TSK_LIT_ENDIAN, (uint8_t *) diskData.data());
            initialized = true;
            //cout << *superblock << endl;
        }

        //TODO: check if device is part of pool
        dev = new BTRFS_DEVICE(supblk->getDevData().getID(), supblk->getDevData().getUUID().encode(),
                               it.second, 0, true);
        //cerr << "DBG: Added device with id " << dev->getID() << endl;
        devices.push_back(dev);
        no_available_devices++;
        delete supblk;
    }


    //initialize global treeexaminer for BTRFS_POOL
    //TODO: rootFsId is currently fixed
    //cerr << "DBG: Creating TreeExaminer" << endl;
    uint64_t rootFsId = 0;
    if (rootFsId == 0)
        examiner = new TreeExaminer(this, TSK_LIT_ENDIAN, superblock);
    else
        examiner = new TreeExaminer(this, TSK_LIT_ENDIAN, superblock, rootFsId);

    //cerr << "DBG: Created TreeExaminer" << endl;
    examiner->initializeRootTree(superblock);
    examiner->initializeFSTree();
}


BTRFS_POOL::~BTRFS_POOL() {
    for (auto it : devices) {
        delete it;
    }
    delete superblock;
}

void BTRFS_POOL::readData(uint64_t logical_addr, uint64_t size, vector<char> &buffer, bool fillWithZeros) {
    vector <BTRFSPhyAddr> physicalAddr;
    uint64_t offset = logical_addr;
    vector<char> stripeData;
    buffer.resize(0);
    //TODO: variable stripe_length !!!
    uint64_t stripe_length = 65536;
    uint64_t num_stripes = (uint64_t) ceil(size / stripe_length);
    uint64_t size_left = size;
    uint64_t size_for_stripe = 0;
    uint64_t chunk_logical = this->getChunkLogicalAddr(logical_addr);
    //printf("input_log %llu | chunk_log %llu | size %llu | str_len %u | num_stripes %d\n", offset, chunk_logical, size,
    //       stripe_length, num_stripes);

    bool read;
    while (size_left > 0) {
        read = false;
        size_for_stripe = stripe_length - ((offset - chunk_logical) % stripe_length);
        if (size_for_stripe > size_left) {
            size_for_stripe = size_left;
        }
        //cerr << "DBG: SIZE FOR STRIPE: " << size_for_stripe << "\t SIZE LEFT: " << size_left << endl;

        physicalAddr = this->getPhysicalAddress(offset);
        //check if any of these addresses is available
        stripeData.resize(size_for_stripe);
        for (auto &it : physicalAddr) {
            if (getDeviceByID(it.device) != nullptr) {
                //cerr << "DBG: Found data on device " << it.device << " @ " << it.offset << endl;
                readRawData(it.device, it.offset, size_for_stripe, stripeData);
                read = true;
                break;
            }
        }

        if (!read && fillWithZeros) {
            std::fill(stripeData.begin(), stripeData.end(), 0);
        }

        buffer.insert(buffer.end(), stripeData.data(), stripeData.data() + size_for_stripe);
        size_left -= size_for_stripe;
        offset += size_for_stripe;
    }
}

void BTRFS_POOL::readRawData(int dev_id, uint64_t offset, uint64_t size, vector<char> &buffer) {
    BTRFS_DEVICE *dev = getDeviceByID(dev_id);
    if (dev == nullptr) {
        cerr << "Cannot find device with ID << " << dev_id << endl;
        throw "Device with ID does not exist in this pool!";
    }
    buffer.resize(size);
    tsk_img_read(dev->getImg(), offset, buffer.data(), size);
}

uint64_t BTRFS_POOL::getChunkLogicalAddr(uint64_t logical_address) {
    //cerr << "DBG: getChunkLogicalAddr;  " << logical_address << endl;
    if(examiner == nullptr){
        //chunk tree is not yet ready, so returning the logical addr. of the correct system chunk from superblock
        uint64_t chunkLogical = 0;
        for(int i=0; i < superblock->getSysChunkSize(); i++){
            if(superblock->getChunkKey(i).offset <= logical_address){
                chunkLogical = superblock->getChunkKey(i).offset;
            } else {
                break;
            }
        }
    //TODO: exception if not found
        return chunkLogical;
    } else {
        return examiner->getChunkLogicalAddr(logical_address);
    }
}


vector <BTRFSPhyAddr> BTRFS_POOL::getPhysicalAddress(uint64_t logical_address) {
    //TODO: initialized check function for examiner
    if(examiner == nullptr){
        //chunk tree is not yet ready, so returning the logical addr. of the correct system chunk from superblock
        int found_index = 0;

        for(int i=0; i < superblock->getSysChunkSize(); i++){
            if(superblock->getChunkKey(i).offset <= logical_address){
                found_index = i;
            } else {
                break;
            }
        }

        BtrfsKey key = superblock->getChunkKey(found_index);
        ChunkData data = superblock->getChunkData(found_index);
        return getChunkAddr(logical_address, &key, &data);
    } else {
        return examiner->getPhysicalAddr(logical_address);
    }
}

BTRFS_DEVICE *BTRFS_POOL::getDeviceByID(uint64_t id) const {
    for (int i = 0; i < devices.size(); i++) {
        if (devices.at(i)->getID() == id)
            return devices.at(i);
    }
    return nullptr;
}

void BTRFS_POOL::printChunkInformation(std::ostream &os) const {
    if (examiner != nullptr) {
        examiner->chunkTree->chunkRoot;
        vector<const BtrfsItem *> foundChunks;
        examiner->treeTraverse(examiner->chunkTree->chunkRoot, [&foundChunks](const LeafNode *leaf) {
            filterItems(leaf, ItemType::CHUNK_ITEM, foundChunks);
        });

        uint64_t system_chunks = 0;
        uint64_t system_chunks_available = 0;
        uint64_t system_chunks_type = 0;
        uint64_t data_chunks = 0;
        uint64_t data_chunks_available = 0;
        uint64_t data_chunks_type = 0;
        uint64_t metadata_chunks = 0;
        uint64_t metadata_chunks_available = 0;
        uint64_t metadata_chunks_type = 0;


        for (auto chunk : foundChunks) {
            const ChunkItem *chunk_item = static_cast<const ChunkItem *>(chunk);
            if (chunk_item->getType() & BLOCK_FLAG_SYSTEM) {
                system_chunks++;
                if (isChunkDataAvailable(chunk_item))
                    system_chunks_available++;
                if (!system_chunks_type){
                    system_chunks_type = chunk_item->getType();
                }
            } else if (chunk_item->getType() & BLOCK_FLAG_METADATA) {
                metadata_chunks++;
                if (isChunkDataAvailable(chunk_item))
                    metadata_chunks_available++;
                if (!metadata_chunks_type){
                    metadata_chunks_type = chunk_item->getType();
                }
            } else if (chunk_item->getType() & BLOCK_FLAG_DATA) {
                data_chunks++;
                if (isChunkDataAvailable(chunk_item))
                    data_chunks_available++;
                if (!data_chunks_type){
                    data_chunks_type = chunk_item->getType();
                }
            }
        }

        os << setw(20) << left << "System chunks:" << getRAIDFromFlag(system_chunks_type) << " (" << system_chunks_available << "/"
             << system_chunks << ")" << endl;
        os << setw(20) << left << "Metadata chunks:" << getRAIDFromFlag(metadata_chunks_type) << " (" << metadata_chunks_available << "/"
             << metadata_chunks << ")" << endl;
        os << setw(20) << left << "Data chunks:"  << getRAIDFromFlag(data_chunks_type) << " (" << data_chunks_available << "/"
             << data_chunks << ")" << endl;
    }
}

bool BTRFS_POOL::isChunkDataAvailable(const ChunkItem *item) const {
    bool available = true;

    if (item->getType() & BLOCK_FLAG_RAID0) {
        for (int i = 0; i < item->data.getNumStripe(); i++) {
            if ((getDeviceByID(item->data.getID(i)) == nullptr)) {
                available = false;
                break;
            }
        }
    } else if (item->getType() & BLOCK_FLAG_RAID1) {
        available = false;
        for (int i = 0; i < item->data.getNumStripe(); i++) {
            if ((getDeviceByID(item->data.getID(i)) != nullptr)) {
                available = true;
                break;
            }
        }
    } else if (item->getType() & BLOCK_FLAG_RAID10) {
        available = true;
        bool available_second_raid = true;
        for (int i = 0; i < item->data.getNumStripe() / 2; i++) {
            if ((getDeviceByID(item->data.getID(i)) == nullptr)) {
                available = false;
            }
            if ((getDeviceByID(item->data.getID(i*2 + 1)) == nullptr)) {
                available_second_raid = false;
            }
        }

        available = available or available_second_raid;

    }

    return available;

}

void BTRFS_POOL::print(std::ostream &os) const {
    os << setw(20) << left << "FSID: " << pool_guid << endl;
    printChunkInformation(os);
    os << setw(20) << left << "Number of devices: " << no_all_devices << " (" << no_available_devices << " detected)" << endl;
    os << "-------------------------------------------------" << endl;
    for (auto &it : devices) {
        os << setw(20) << left << "ID: \t" << it->getID() << endl;
        os << setw(20) << left << "GUID:\t" << it->getGUID() << endl;
        os << endl;
    }

    //TODO: not only of one superblock
    os << "Backup Roots:" << endl;
    superblock->printRootBackups(os);


    /*DBG CODE
    for(int i=0; i < superblock->getSysChunkSize(); i++){
        cout << superblock->getChunkKey(i) << endl;
        cout << superblock->getChunkData(i).dataInfo() << endl;
    }*/

}


string BTRFS_POOL::getPoolName() {

    return "N/A";
}

//file system specific functions

void BTRFS_POOL::fsstat(string str_dataset, int uberblock) {
    cout << *superblock << endl;
    cout << superblock->printSpace() << endl;
    cout << endl;

    vector<const BtrfsItem *> foundRootRefs;
    examiner->treeTraverse(examiner->rootTree, [&foundRootRefs](const LeafNode *leaf) {
        filterItems(leaf, ItemType::ROOT_BACKREF, foundRootRefs);
    });

    if (foundRootRefs.size() == 0) {
        cout << "\nNo subvolumes or snapshots are found.\n" << endl;
        return;
    }

    cout << "The following subvolumes or snapshots are found:" << endl;
    for (auto item : foundRootRefs) {
        const RootRef *ref = static_cast<const RootRef *>(item);
        cout << dec << setfill(' ') << setw(7);
        cout << ref->getId() << "   " << ref->getDirName() << '\n';
    }
}

void BTRFS_POOL::fls(string sub_file_system, int generation) {
    uint64_t fsTreeID = 0;
    if (sub_file_system != "") {
        vector<const BtrfsItem *> foundRootRefs;
        examiner->treeTraverse(examiner->rootTree, [&foundRootRefs](const LeafNode *leaf) {
            filterItems(leaf, ItemType::ROOT_BACKREF, foundRootRefs);
        });

        for (auto item : foundRootRefs) {
            const RootRef *ref = static_cast<const RootRef *>(item);
            if (ref->getDirName() == sub_file_system) {
                fsTreeID = ref->getId();
            }
        }

        if (fsTreeID == 0) {
            cerr << "Could not find subvolume/snapshot named " << sub_file_system << " using root instead!" << endl;
        } else {
            examiner->reInitializeFSTree(fsTreeID);
        }
    }

    uint64_t targetId = examiner->fsTree->rootDirId;
    examiner->fsTree->listDirItemsById(targetId, true, true, true, 0, cout);
}


void BTRFS_POOL::istat(int object_number, string sub_file_system, int generation) {
    uint64_t fsTreeID = 0;
    if (sub_file_system != "") {
        vector<const BtrfsItem *> foundRootRefs;
        examiner->treeTraverse(examiner->rootTree, [&foundRootRefs](const LeafNode *leaf) {
            filterItems(leaf, ItemType::ROOT_BACKREF, foundRootRefs);
        });

        for (auto item : foundRootRefs) {
            const RootRef *ref = static_cast<const RootRef *>(item);
            if (ref->getDirName() == sub_file_system) {
                fsTreeID = ref->getId();
            }
        }

        if (fsTreeID == 0) {
            cerr << "Could not find subvolume/snapshot named " << sub_file_system << " using root instead!" << endl;
        } else {
            examiner->reInitializeFSTree(fsTreeID);
        }
    }

    uint64_t targetId = examiner->fsTree->rootDirId;

    examiner->fsTree->showInodeInfo(object_number, cout);
}

void BTRFS_POOL::icat(int object_number, string sub_file_system, int generation) {
    uint64_t fsTreeID = 0;
    if (sub_file_system != "") {
        vector<const BtrfsItem *> foundRootRefs;
        examiner->treeTraverse(examiner->rootTree, [&foundRootRefs](const LeafNode *leaf) {
            filterItems(leaf, ItemType::ROOT_BACKREF, foundRootRefs);
        });

        for (auto item : foundRootRefs) {
            const RootRef *ref = static_cast<const RootRef *>(item);
            if (ref->getDirName() == sub_file_system) {
                fsTreeID = ref->getId();
            }
        }

        if (fsTreeID == 0) {
            cerr << "Could not find subvolume/snapshot named " << sub_file_system << " using root instead!" << endl;
        } else {
            examiner->reInitializeFSTree(fsTreeID);
        }
    }

    uint64_t targetId = examiner->fsTree->rootDirId;

    examiner->fsTree->readFile(object_number);
}