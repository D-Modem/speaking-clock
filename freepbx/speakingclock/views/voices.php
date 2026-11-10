<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// views/voices.php - the voices in the voice folder.  Given $voices, $dir.

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}
use FreePBX\modules\Speakingclock as S;
?>
<p class="text-muted">
	<?php echo _('The recordings belong to their original owners and are not covered by the module\'s licence: see NOTICE.md in the voice folder.'); ?>
</p>
<table class="table table-striped" id="speakingclock-voices">
	<thead>
		<tr>
			<th><?php echo _('Voice'); ?></th>
			<th><?php echo _('Name'); ?></th>
			<th><?php echo _('Kind'); ?></th>
			<th><?php echo _('Description'); ?></th>
		</tr>
	</thead>
	<tbody>
		<?php foreach ($voices as $id => $v): ?>
			<tr>
				<td><code><?php echo S::h($id); ?></code></td>
				<td><?php echo S::h($v['name']); ?></td>
				<td><?php echo S::h($v['mode']); ?></td>
				<td><?php echo S::h($v['description']); ?></td>
			</tr>
		<?php endforeach; ?>
	</tbody>
</table>
