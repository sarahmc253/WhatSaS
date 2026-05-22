// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract DataStore {

    struct HashRecord {
        bytes32 hash;
        uint256 timestamp;
        address recorder;
    }

    mapping(uint256 => HashRecord) public records;
    uint256 public recordCount;

    event DataStored(
        uint256 indexed recordId,
        bytes32 indexed dataHash,
        uint256 timestamp,
        address indexed recorder
    );

    function storeData(string calldata data) external returns (uint256 recordId) {
        recordId = recordCount;
        bytes32 dataHash = keccak256(abi.encodePacked(data));

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

    function verifyData(uint256 recordId, string calldata data) external view returns (bool) {
        require(recordId < recordCount, "Record does not exist");
        return records[recordId].hash == keccak256(abi.encodePacked(data));
    }
}
