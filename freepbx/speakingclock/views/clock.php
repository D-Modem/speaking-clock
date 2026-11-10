<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// views/clock.php - a clock's form.  Given $clock, $editing, $errors,
// $voices.

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}
use FreePBX\modules\Speakingclock as S;

include __DIR__ . '/_form.php';
$c = $clock;
$delete = $editing ? 'config.php?display=speakingclock&action=delete&clock=' . urlencode($c['name']) : '';
?>
<form class="fpbx-submit" name="speakingclock_clock" method="post"
      action="config.php?display=speakingclock&amp;view=form<?php echo $editing ? '&amp;clock=' . urlencode($c['name']) : ''; ?>"
      <?php echo $delete !== '' ? 'data-fpbx-delete="' . S::h($delete) . '"' : ''; ?>>
	<input type="hidden" name="action" value="save_clock">
	<input type="hidden" name="original" value="<?php echo $editing ? S::h($c['name']) : ''; ?>">
	<?php
	$row('name', _('Name'), $text('name', $c['name'], $editing ? ' readonly' : ''),
		_('The clock\'s destination is app-speakingclock,<name>,1, so the name can\'t change once saved.'));
	$row('description', _('Description'), $text('description', $c['description']),
		_('Shown in the destination lists.'));
	$row('voice', _('Voice'), $select('voice', $c['voice'], $voiceOptions($voices, $c['voice'])),
		_('tim voices give hours, minutes and seconds with pips, every 10 seconds. talking voices give the minute. chime and hourly voices only chime.'));
	$row('number', _('Number to dial'), $text('number', $c['number'], ' placeholder="123"'),
		_('Optional. Your extensions can dial it (it is added to from-internal-additional). Make sure nothing else uses it.'));
	$row('timezone', _('Time zone'), $zone('timezone', $c['timezone'], _('Default (Settings)')),
		_('The time zone the clock speaks in.'));
	$row('hours24', _('24-hour clock'), $check('hours24', $c['hours24']),
		_('Say 13 to 23 for the afternoon, with no am or pm.'));
	$row('period', _('Seconds between announcements'), $text('period', $c['period']),
		_('0 for the voice\'s own (10 for most voices).'));
	$row('duration', _('Hang up after (seconds)'), $text('duration', $c['duration'], ' placeholder="' . S::h(_('default')) . '"'),
		_('Empty for the default under Settings, 0 for never.'));
	$row('wait', _('Wait for the start'), $check('wait', $c['wait']),
		_('Callers hear silence until the next announcement starts, instead of joining one part way through as on the real clocks.'));
	$row('exit_key', _('Any key leaves'), $check('exit_key', $c['exit_key']),
		_('A key press ends the clock and the call goes on in the dialplan (it hangs up here).'));
	?>
</form>
