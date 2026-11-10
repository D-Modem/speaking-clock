<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// views/clocks.php - the clocks.  Given $clocks, $voices.

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}
use FreePBX\modules\Speakingclock as S;
?>
<p>
	<a href="?display=speakingclock&amp;view=form" class="btn btn-default">
		<i class="fa fa-plus"></i> <?php echo _('Add clock'); ?></a>
</p>
<?php if (!$clocks): ?>
	<div class="alert alert-info">
		<?php echo _('No clocks yet. A clock is a voice with its time zone. Each one is a destination for inbound routes, IVRs and time conditions, under "Speaking Clock", and can have a number your extensions dial, such as 123.'); ?>
	</div>
<?php else: ?>
	<table class="table table-striped" id="speakingclock-clocks">
		<thead>
			<tr>
				<th><?php echo _('Clock'); ?></th>
				<th><?php echo _('Description'); ?></th>
				<th><?php echo _('Voice'); ?></th>
				<th><?php echo _('Number'); ?></th>
				<th><?php echo _('Time zone'); ?></th>
				<th><?php echo _('Destination'); ?></th>
			</tr>
		</thead>
		<tbody>
			<?php foreach ($clocks as $name => $c): ?>
				<tr>
					<td><a href="?display=speakingclock&amp;view=form&amp;clock=<?php echo urlencode($name); ?>">
						<?php echo S::h($name); ?></a></td>
					<td><?php echo S::h($c['description']); ?></td>
					<td><?php echo S::h(isset($voices[$c['voice']]) ? $voices[$c['voice']]['name'] : $c['voice'] . ' ' . _('(missing)')); ?></td>
					<td><?php echo S::h($c['number']); ?></td>
					<td><?php echo S::h($c['timezone'] !== '' ? $c['timezone'] : _('default')); ?></td>
					<td><code><?php echo S::h(S::destination($name)); ?></code></td>
				</tr>
			<?php endforeach; ?>
		</tbody>
	</table>
<?php endif; ?>
