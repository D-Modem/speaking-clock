<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// views/settings.php - speakingclock.conf's settings.  Given $s, $errors,
// $voices.

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}
use FreePBX\modules\Speakingclock as S;

include __DIR__ . '/_form.php';
?>
<form class="fpbx-submit" name="speakingclock_settings" method="post"
      action="config.php?display=speakingclock&amp;view=settings">
	<input type="hidden" name="action" value="save_settings">
	<?php
	$row('voicedir', _('Voice folder'), $text('voicedir', $s['voicedir']),
		_('Where the voice folders are: Asterisk\'s sounds/speaking-clock once make install has run.'));
	$row('default_voice', _('Default voice'), $select('default_voice', $s['default_voice'], $voiceOptions($voices, $s['default_voice'])),
		_('For SpeakingClock() with no voice, and for new clocks.'));
	$row('timezone', _('Default time zone'), $zone('timezone', $s['timezone'], _('The system\'s')),
		_('For clocks without one of their own.'));
	$row('maxduration', _('Hang up after (seconds)'), $text('maxduration', $s['maxduration']),
		_('How long a call may listen, 0 for no limit.'));
	?>
</form>
