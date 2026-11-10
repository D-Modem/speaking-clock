<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// views/_form.php - form helpers for clock.php and settings.php: $row,
// $text, $select, $check, $zone.  Given $errors.

if (!defined('FREEPBX_IS_AUTH')) {
	die('No direct script access allowed');
}
use FreePBX\modules\Speakingclock as S;

$row = function ($id, $label, $field, $help) use ($errors) {
	$err = isset($errors[$id]) ? $errors[$id] : '';
	?>
	<div class="element-container<?php echo $err !== '' ? ' has-error' : ''; ?>" data-field="<?php echo S::h($id); ?>">
		<div class="row form-group">
			<div class="col-md-3">
				<label class="control-label" for="<?php echo S::h($id); ?>"><?php echo S::h($label); ?></label>
			</div>
			<div class="col-md-9">
				<?php echo $field; ?>
				<?php if ($err !== ''): ?>
					<span class="help-block speakingclock-error"><?php echo S::h($err); ?></span>
				<?php endif; ?>
				<span class="help-block"><?php echo S::h($help); ?></span>
			</div>
		</div>
	</div>
	<?php
};
$text = function ($id, $value, $extra = '') {
	return '<input type="text" class="form-control" id="' . S::h($id) .
		'" name="' . S::h($id) . '" value="' . S::h($value) . '"' . $extra . '>';
};
$select = function ($id, $value, array $options) {
	$h = '<select class="form-control" id="' . S::h($id) . '" name="' . S::h($id) . '">';
	foreach ($options as $v => $label)
		$h .= '<option value="' . S::h($v) . '"' .
			((string)$v === (string)$value ? ' selected' : '') . '>' .
			S::h($label) . '</option>';
	return $h . '</select>';
};
$check = function ($id, $on) {
	return '<input type="checkbox" id="' . S::h($id) . '" name="' . S::h($id) .
		'" value="yes"' . ($on ? ' checked' : '') . '>';
};
/* a time zone select; '' is the default */
$zone = function ($id, $value, $defaultLabel) use ($select) {
	$o = ['' => $defaultLabel];
	foreach (\DateTimeZone::listIdentifiers() as $z)
		$o[$z] = $z;
	return $select($id, $value, $o);
};
/* voice id => "Name (id)", with the current one kept if it is missing */
$voiceOptions = function (array $voices, $current) {
	$o = [];
	foreach ($voices as $id => $v)
		$o[$id] = $v['name'] . ' (' . $id . ', ' . $v['mode'] . ')';
	if ($current !== '' && !isset($o[$current]))
		$o[$current] = $current . ' ' . _('(missing)');
	return $o;
};
