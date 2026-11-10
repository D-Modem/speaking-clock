<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// page.speakingclock.php - Applications > Speaking Clock

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}
echo \FreePBX::Speakingclock()->showPage();
