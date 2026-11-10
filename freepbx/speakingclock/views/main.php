<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// views/main.php - the page: tabs, the last action's message, the body.
// Given $view, $message, $settings, $voices, $body.

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}
use FreePBX\modules\Speakingclock as S;

$tabs = [
	'' => _('Clocks'),
	'voices' => _('Voices'),
	'settings' => _('Settings'),
	'dialplan' => _('Generated files'),
];
$active = $view === 'form' ? '' : $view;
?>
<div class="container-fluid" id="speakingclock">
	<h1><?php echo _('Speaking Clock'); ?></h1>
	<p class="text-muted">
		<?php echo _('Telephone speaking clocks for your callers, timed against the system clock so the pips fall on the second.'); ?>
		<?php echo sprintf(_('%d voices in %s.'), count($voices), S::h($settings['voicedir'])); ?>
	</p>
	<?php if (!$voices): ?>
		<div class="alert alert-warning">
			<?php echo sprintf(_('No voices found in %s. Install app_speakingclock and its voices (make install in the speaking-clock repository\'s asterisk folder), or set the voice folder under Settings.'), S::h($settings['voicedir'])); ?>
		</div>
	<?php endif; ?>
	<ul class="nav nav-tabs" role="tablist">
		<?php foreach ($tabs as $k => $label): ?>
			<li role="presentation" class="nav-item<?php echo $k === $active ? ' active' : ''; ?>">
				<a class="nav-link<?php echo $k === $active ? ' active' : ''; ?>"
				   href="?display=speakingclock<?php echo $k !== '' ? '&amp;view=' . S::h($k) : ''; ?>">
					<?php echo S::h($label); ?></a>
			</li>
		<?php endforeach; ?>
	</ul>
	<?php if ($message): ?>
		<div class="alert alert-<?php echo S::h($message[0]); ?>" style="margin-top: 1em">
			<?php echo S::h($message[1]); ?>
		</div>
	<?php endif; ?>
	<div style="margin-top: 1em">
		<?php echo $body; ?>
	</div>
</div>
