<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// views/dialplan.php - what Apply Config writes.  Given $conf, $dialplan.

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}
use FreePBX\modules\Speakingclock as S;
?>
<h3>speakingclock.conf</h3>
<pre><?php echo S::h($conf); ?></pre>
<h3><?php echo _('Dialplan'); ?></h3>
<pre><?php echo S::h($dialplan); ?></pre>
