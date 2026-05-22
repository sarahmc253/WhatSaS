// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract DataStore {

    struct HashRecord {
        bytes32 hash;
        uint256 timestamp;
        address recorder;
    }

    mapping(uint256 => HashRecord) private records;
    uint256 public recordCount;

    event DataStored(
        uint256 indexed recordId,
        bytes32 indexed dataHash,
        uint256 timestamp,
        address indexed recorder
    );

    function storeData(bytes32 dataHash) external returns (uint256 recordId) {
        recordId = recordCount;

        records[recordId] = HashRecord({
            hash: dataHash,
            timestamp: block.timestamp,
            recorder: msg.sender
        });

        recordCount++;
        emit DataStored(recordId, dataHash, block.timestamp, msg.sender);
    }

    function getRecord(uint256 recordId) external view returns (bytes32 hash, uint256 timestamp, address recorder) {
        require(recordId < recordCount, "Record does not exist");
        HashRecord storage r = records[recordId];
        return (r.hash, r.timestamp, r.recorder);
    }

    function verifyData(uint256 recordId, bytes32 dataHash) external view returns (bool) {
        require(recordId < recordCount, "Record does not exist");
        return records[recordId].hash == dataHash;
    }
}
