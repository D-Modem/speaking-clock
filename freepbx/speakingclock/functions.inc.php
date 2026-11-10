<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// functions.inc.php - FreePBX's destination hooks, found by the module's
// name: each clock is a destination of inbound routes, IVRs and the like.

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}

function speakingclock_destinations()
{
	return \FreePBX::Speakingclock()->destinations();
}

function speakingclock_getdest($exten)
{
	return [\FreePBX\modules\Speakingclock::destination($exten)];
}

function speakingclock_getdestinfo($dest)
{
	return \FreePBX::Speakingclock()->destinationInfo($dest);
}
