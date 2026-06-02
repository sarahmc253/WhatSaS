import json
import logging
import threading
import uuid
from datetime import datetime, timezone
from pathlib import Path

import mysql.connector
from flask import current_app
from web3 import Web3

logger = logging.getLogger(__name__)

_ABI_PATH = Path(__file__).parents[3] / 'contracts' / 'abi.json'
with open(_ABI_PATH) as _f:
    _ABI = json.load(_f)

_lock = threading.Lock()
_confirm_lock = threading.Lock()


def _connect():
    cfg = current_app.config
    return mysql.connector.connect(
        host=cfg['DB_HOST'],
        port=cfg['DB_PORT'],
        user=cfg['DB_USER'],
        password=cfg['DB_PASSWORD'],
        database=cfg['DB_NAME'],
    )


def _merkle_root(hex_hashes):
    layer = [bytes.fromhex(h.removeprefix('0x')) for h in hex_hashes]
    while len(layer) > 1:
        if len(layer) % 2 == 1:
            layer.append(layer[-1])
        layer = [
            Web3.keccak(primitive=layer[i] + layer[i + 1])
            for i in range(0, len(layer), 2)
        ]
    return '0x' + layer[0].hex()


def anchor_pending(user_id=None):
    """Fetch unanchored messages, build a Merkle root per conversation, and anchor on-chain.

    Must be called within a Flask application context.
    """
    logger.info('anchor_pending called (user_id=%s)', user_id)

    if not _lock.acquire(blocking=False):
        logger.warning('anchor_pending already running, skipping')
        return

    logger.info('anchor_pending acquired lock, connecting to DB')
    try:
        db = _connect()
        logger.info('anchor_pending DB connection established')
        try:
            _run(db, user_id)
        finally:
            db.close()
            logger.info('anchor_pending DB connection closed')
    except Exception:
        logger.exception('anchor_pending failed with unhandled exception')
    finally:
        _lock.release()
        logger.info('anchor_pending released lock')


def _run(db, user_id):
    cursor = db.cursor(dictionary=True)
    try:
        if user_id:
            logger.info('querying unanchored messages for user_id=%s', user_id)
            cursor.execute(
                """
                SELECT id, content_hash, recipient_id
                FROM messages
                WHERE blockchain_record_id IS NULL
                  AND content_hash IS NOT NULL
                  AND (sender_id = %s OR recipient_id = %s)
                ORDER BY recipient_id, created_at
                """,
                (user_id, user_id),
            )
        else:
            logger.info('querying all unanchored messages (no user filter)')
            cursor.execute(
                """
                SELECT id, content_hash, recipient_id
                FROM messages
                WHERE blockchain_record_id IS NULL
                  AND content_hash IS NOT NULL
                ORDER BY recipient_id, created_at
                """
            )
        rows = cursor.fetchall()
    finally:
        cursor.close()

    logger.info('found %d unanchored message(s)', len(rows))
    if not rows:
        logger.info('nothing to anchor, returning early')
        return

    groups = {}
    for row in rows:
        groups.setdefault(row['recipient_id'], []).append(row)

    logger.info('grouped into %d recipient conversation(s)', len(groups))

    cfg = current_app.config
    logger.info('connecting to Web3 RPC: %s', cfg.get('WEB3_RPC_URL', '<not set>'))
    w3 = Web3(Web3.HTTPProvider(cfg['WEB3_RPC_URL']))

    if not w3.is_connected():
        logger.error('Web3 is not connected to RPC %s — aborting anchor', cfg.get('WEB3_RPC_URL'))
        return

    logger.info('Web3 connected, contract address: %s', cfg.get('CONTRACT_ADDRESS'))
    contract = w3.eth.contract(
        address=Web3.to_checksum_address(cfg['CONTRACT_ADDRESS']),
        abi=_ABI,
    )
    account = w3.eth.account.from_key(cfg['WALLET_PRIVATE_KEY'])
    nonce = w3.eth.get_transaction_count(account.address)
    logger.info('wallet address: %s, starting nonce: %d', account.address, nonce)

    for recipient_id, msgs in groups.items():
        logger.info('anchoring %d message(s) for recipient %s', len(msgs), recipient_id)
        root = _merkle_root([m['content_hash'] for m in msgs])
        logger.info('merkle root for recipient %s: %s', recipient_id, root)

        ids = [m['id'] for m in msgs]
        record_id = str(uuid.uuid4())
        placeholders = ','.join(['%s'] * len(ids))

        # Reserve rows before touching the chain so a concurrent scheduler run
        # won't pick up the same messages (blockchain_record_id will be non-NULL).
        logger.info('reserving blockchain_record %s in DB for recipient %s', record_id, recipient_id)
        cursor = db.cursor()
        try:
            cursor.execute(
                """
                INSERT INTO blockchain_records
                    (id, merkle_root, recipient_id, tx_hash, block_number, block_timestamp)
                VALUES (%s, %s, %s, NULL, NULL, NULL)
                """,
                (record_id, root, recipient_id),
            )
            cursor.execute(
                f'UPDATE messages SET blockchain_record_id = %s WHERE id IN ({placeholders})',
                (record_id, *ids),
            )
        except Exception:
            db.rollback()
            logger.exception('DB reserve failed for recipient %s — rolled back', recipient_id)
            continue
        finally:
            cursor.close()

        logger.info('DB reservation committed, sending chain tx for recipient %s', recipient_id)

        # Send chain tx; roll back the reservation on failure so messages can be retried.
        try:
            root_bytes = bytes.fromhex(root.removeprefix('0x'))
            estimated_gas = contract.functions.storeData(root_bytes).estimate_gas({'from': account.address})
            logger.info('estimated gas: %d', estimated_gas)
            tx = contract.functions.storeData(root_bytes).build_transaction({
                'from': account.address,
                'nonce': nonce,
                'gas': int(estimated_gas * 1.2),
                'gasPrice': w3.eth.gas_price,
            })
            signed = account.sign_transaction(tx)
            raw_hash = w3.eth.send_raw_transaction(signed.raw_transaction)
            nonce += 1
            tx_hash_hex = '0x' + raw_hash.hex()
            logger.info('tx sent for recipient %s: %s', recipient_id, tx_hash_hex)
        except Exception:
            db.rollback()
            logger.exception('chain tx failed for recipient %s — reservation rolled back', recipient_id)
            continue

        cursor = db.cursor()
        try:
            cursor.execute(
                'UPDATE blockchain_records SET tx_hash = %s WHERE id = %s',
                (tx_hash_hex, record_id),
            )
            db.commit()
            logger.info('anchor complete for recipient %s, record %s', recipient_id, record_id)
        except Exception:
            db.rollback()
            logger.exception('DB commit failed for recipient %s', recipient_id)
        finally:
            cursor.close()


def confirm_pending():
    """Check unconfirmed blockchain_records and update block_number/block_timestamp on receipt.

    Must be called within a Flask application context.
    """
    logger.info('confirm_pending called')

    if not _confirm_lock.acquire(blocking=False):
        logger.warning('confirm_pending already running, skipping')
        return

    try:
        db = _connect()
        try:
            _confirm_run(db)
        finally:
            db.close()
    except Exception:
        logger.exception('confirm_pending failed with unhandled exception')
    finally:
        _confirm_lock.release()


def _confirm_run(db):
    cursor = db.cursor(dictionary=True)
    try:
        cursor.execute(
            'SELECT id, tx_hash, revert_count FROM blockchain_records WHERE block_number IS NULL'
        )
        rows = cursor.fetchall()
    finally:
        cursor.close()

    logger.info('confirm_pending: %d unconfirmed record(s)', len(rows))
    if not rows:
        return

    cfg = current_app.config
    w3 = Web3(Web3.HTTPProvider(cfg['WEB3_RPC_URL']))

    for row in rows:
        logger.info('checking receipt for tx %s', row['tx_hash'])
        try:
            receipt = w3.eth.get_transaction_receipt(row['tx_hash'])
        except Exception:
            logger.exception('receipt fetch failed for tx %s', row['tx_hash'])
            continue

        if receipt is None:
            logger.info('tx %s not yet mined', row['tx_hash'])
            continue

        if receipt['status'] != 1:
            new_count = (row['revert_count'] or 0) + 1
            if new_count > 3:
                logger.warning(
                    'tx %s reverted %d times (status=%s), releasing messages for re-anchor',
                    row['tx_hash'], new_count, receipt['status'],
                )
                cursor = db.cursor()
                try:
                    cursor.execute(
                        'UPDATE messages SET blockchain_record_id = NULL WHERE blockchain_record_id = %s',
                        (row['id'],),
                    )
                    cursor.execute(
                        'DELETE FROM blockchain_records WHERE id = %s',
                        (row['id'],),
                    )
                    db.commit()
                except Exception:
                    db.rollback()
                    logger.exception('failed to release reverted record %s', row['id'])
                finally:
                    cursor.close()
            else:
                logger.warning(
                    'tx %s reverted (status=%s), revert_count now %d, leaving messages claimed',
                    row['tx_hash'], receipt['status'], new_count,
                )
                cursor = db.cursor()
                try:
                    cursor.execute(
                        'UPDATE blockchain_records SET revert_count = %s WHERE id = %s',
                        (new_count, row['id']),
                    )
                    db.commit()
                except Exception:
                    db.rollback()
                    logger.exception('failed to increment revert_count for record %s', row['id'])
                finally:
                    cursor.close()
            continue

        try:
            block = w3.eth.get_block(receipt['blockNumber'])
            block_number = receipt['blockNumber']
            block_timestamp = datetime.fromtimestamp(block['timestamp'], tz=timezone.utc)
        except Exception:
            logger.exception('block fetch failed for tx %s', row['tx_hash'])
            continue

        cursor = db.cursor()
        try:
            cursor.execute(
                """
                UPDATE blockchain_records
                SET block_number = %s, block_timestamp = %s
                WHERE id = %s
                """,
                (block_number, block_timestamp, row['id']),
            )
            db.commit()
            logger.info('confirmed tx %s at block %s', row['tx_hash'], block_number)
        except Exception:
            db.rollback()
            logger.exception('DB update failed for blockchain_record %s', row['id'])
        finally:
            cursor.close()
