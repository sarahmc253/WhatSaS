CREATE TABLE `users` (
  `id` char(36) NOT NULL DEFAULT (uuid()),
  `username` varchar(64) NOT NULL,
  `email` varchar(255) NOT NULL,
  `password_hash` text NOT NULL,
  `password_salt` text NOT NULL,
  `kek_salt` text NOT NULL,
  `x25519_public_key` text NOT NULL,
  `wrapped_private_key` text NOT NULL,
  `tofu_key_pinned_at` timestamp NULL DEFAULT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uq_users_username` (`username`),
  UNIQUE KEY `uq_users_email` (`email`)
);

CREATE TABLE `messages` (
  `id` char(36) NOT NULL DEFAULT (uuid()),
  `sender_id` char(36) NOT NULL,
  `recipient_id` char(36) NOT NULL,
  `ciphertext` text NOT NULL,
  `nonce` text NOT NULL,
  `ephemeral_pk` text NOT NULL,
  `is_revoked` tinyint(1) NOT NULL DEFAULT '0',
  `original_message_id` char(36) DEFAULT NULL,
  `blockchain_record_id` char(36) DEFAULT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `timestamp` int NOT NULL DEFAULT 0,
  `content_hash` char(66) DEFAULT NULL,
  PRIMARY KEY (`id`),
  ...foreign keys...
);

CREATE TABLE `blockchain_records` (
  `id` char(36) NOT NULL DEFAULT (uuid()),
  `tx_hash` varchar(128) DEFAULT NULL,
  `block_number` bigint unsigned DEFAULT NULL,
  `block_timestamp` timestamp NULL DEFAULT NULL,
  `recorded_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `merkle_root` varchar(66) NOT NULL,
  `recipient_id` char(36) NOT NULL,
  `revert_count` int NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  ...foreign keys...
);

CREATE TABLE `audit_log` (
  `id` char(36) NOT NULL DEFAULT (uuid()),
  `user_id` char(36) DEFAULT NULL,
�  `message_id` char(36) DEFAULT NULL,
  `action` enum('login','logout','send_message',...) NOT NULL,
  `ip_address` varchar(45) DEFAULT NULL,
  `metadata` text DEFAULT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  ...foreign keys...
);