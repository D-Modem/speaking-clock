<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// Clock.php - the module's data without FreePBX: a clock's and the
// settings' defaults, how a form's values are cleaned and checked, the
// voices found in the voice folder, speakingclock.conf, and the
// app-speakingclock context (an extension per clock, and per number).

namespace FreePBX\modules\Speakingclock;

class Clock
{
	const CONTEXT = 'app-speakingclock';
	/* the context that extensions dial from, which includes ours */
	const INCLUDE_IN = 'from-internal-additional';

	public static function defaultSettings()
	{
		return [
			'voicedir' => '/var/lib/asterisk/sounds/speaking-clock',
			'default_voice' => 'tim-pat-simmons',
			'timezone' => '',
			'maxduration' => 180,
			'tts_program_dectalk' => '',
			'tts_program_sam' => '',
		];
	}

	public static function defaultClock()
	{
		return [
			'name' => '',
			'description' => '',
			'voice' => 'tim-pat-simmons',
			'number' => '',
			'timezone' => '',
			'hours24' => false,
			'period' => 0,          // 0: the voice's
			'duration' => '',       // '': the settings' maxduration
			'wait' => false,
			'exit_key' => false,
		];
	}

	private static function str($a, $k, $def = '')
	{
		return isset($a[$k]) && is_scalar($a[$k]) ? trim((string)$a[$k]) : $def;
	}

	private static function yes($a, $k)
	{
		if (!isset($a[$k]))
			return false;
		$v = $a[$k];
		return $v === true || $v === 1 || $v === '1' || $v === 'yes' || $v === 'on';
	}

	public static function normalizeSettings(array $a)
	{
		$d = self::defaultSettings();
		return [
			'voicedir' => rtrim(self::str($a, 'voicedir', $d['voicedir']), '/'),
			'default_voice' => self::str($a, 'default_voice', $d['default_voice']),
			'timezone' => self::str($a, 'timezone'),
			'maxduration' => self::str($a, 'maxduration', (string)$d['maxduration']),
			'tts_program_dectalk' => self::str($a, 'tts_program_dectalk'),
			'tts_program_sam' => self::str($a, 'tts_program_sam'),
		];
	}

	public static function normalizeClock(array $a)
	{
		return [
			'name' => self::str($a, 'name'),
			'description' => self::str($a, 'description'),
			'voice' => self::str($a, 'voice'),
			'number' => self::str($a, 'number'),
			'timezone' => self::str($a, 'timezone'),
			'hours24' => self::yes($a, 'hours24'),
			'period' => self::str($a, 'period', '0'),
			'duration' => self::str($a, 'duration'),
			'wait' => self::yes($a, 'wait'),
			'exit_key' => self::yes($a, 'exit_key'),
		];
	}

	public static function zoneOk($z)
	{
		return $z === '' || in_array($z, \DateTimeZone::listIdentifiers(), true);
	}

	/* a voice's id: a folder name that can't climb out of voicedir */
	public static function voiceIdOk($v)
	{
		return (bool)preg_match('/^[A-Za-z0-9_][A-Za-z0-9_.-]{0,95}$/', $v);
	}

	/* field => error for settings s */
	public static function validateSettings(array $s)
	{
		$e = [];
		if ($s['voicedir'] === '' || $s['voicedir'][0] !== '/' ||
		    preg_match('/[\r\n;]/', $s['voicedir']))
			$e['voicedir'] = _('A full path, such as /var/lib/asterisk/sounds/speaking-clock');
		if (!self::voiceIdOk($s['default_voice']))
			$e['default_voice'] = _('Choose a voice');
		if (!self::zoneOk($s['timezone']))
			$e['timezone'] = _('Not a time zone');
		if (!preg_match('/^\d{1,6}$/', (string)$s['maxduration']))
			$e['maxduration'] = _('Seconds, 0 for no limit');
		foreach (['tts_program_dectalk', 'tts_program_sam'] as $k)
			if ($s[$k] !== '' && !preg_match('#^/[^\s;]*\z#', $s[$k]))
				$e[$k] = _('A full path, with no spaces');
		return $e;
	}

	/* field => error for clock c; taken: the names and numbers other
	   clocks have */
	public static function validateClock(array $c, array $takenNames = [], array $takenNumbers = [])
	{
		$e = [];
		if (!preg_match('/^[a-z][a-z0-9_-]{0,31}$/', $c['name']))
			$e['name'] = _('Lower-case letters, digits, - and _, starting with a letter (up to 32)');
		elseif (in_array($c['name'], $takenNames, true))
			$e['name'] = _('There is a clock of that name');
		if (preg_match('/[\r\n]/', $c['description']) || strlen($c['description']) > 100)
			$e['description'] = _('One line, up to 100 characters');
		if (!self::voiceIdOk($c['voice']))
			$e['voice'] = _('Choose a voice');
		if ($c['number'] !== '') {
			if (!preg_match('/^[0-9*#]{1,20}$/', $c['number']))
				$e['number'] = _('Digits, * and # only');
			elseif (in_array($c['number'], $takenNumbers, true))
				$e['number'] = _('Another clock has this number');
		}
		if (!self::zoneOk($c['timezone']))
			$e['timezone'] = _('Not a time zone');
		if (!preg_match('/^\d{1,4}$/', (string)$c['period']) || (int)$c['period'] > 3600)
			$e['period'] = _('Seconds (0 for the voice\'s own)');
		if ($c['duration'] !== '' && !preg_match('/^\d{1,6}$/', (string)$c['duration']))
			$e['duration'] = _('Seconds, 0 for no limit, empty for the default');
		return $e;
	}

	/* the value of key in [voice] of a voice.conf's text */
	private static function confValue($text, $key)
	{
		$in = false;
		foreach (preg_split('/\r?\n/', $text) as $line) {
			$line = trim($line);
			if ($line === '' || $line[0] === ';')
				continue;
			if ($line[0] === '[') {
				$in = strcasecmp($line, '[voice]') === 0;
				continue;
			}
			if ($in && preg_match('/^' . preg_quote($key, '/') . '\s*=>?\s*(.*)$/i', $line, $m))
				return trim($m[1]);
		}
		return '';
	}

	/* id => ['name', 'description', 'mode'] of the voices in dir, by id */
	public static function voices($dir)
	{
		$out = [];
		$list = @scandir($dir);
		if (!is_array($list))
			return $out;
		foreach ($list as $id) {
			$conf = $dir . '/' . $id . '/voice.conf';
			if (!self::voiceIdOk($id) || !is_file($conf))
				continue;
			$t = (string)@file_get_contents($conf);
			$out[$id] = [
				'name' => self::confValue($t, 'name') ?: $id,
				'description' => self::confValue($t, 'description'),
				'mode' => self::confValue($t, 'mode') ?: 'talking',
			];
		}
		ksort($out, SORT_STRING);
		return $out;
	}

	/* SpeakingClock()'s argument for clock c */
	public static function appArgs(array $c)
	{
		$o = '';
		if ($c['timezone'] !== '')
			$o .= 't(' . $c['timezone'] . ')';
		if ($c['duration'] !== '')
			$o .= 'd(' . (int)$c['duration'] . ')';
		if ((int)$c['period'] > 0)
			$o .= 'p(' . (int)$c['period'] . ')';
		if ($c['hours24'])
			$o .= 'h';
		if ($c['wait'])
			$o .= 'w';
		if ($c['exit_key'])
			$o .= 'x';
		return $c['voice'] . ($o !== '' ? ',' . $o : '');
	}

	/* the context: exten => [[application, data], ...] in order */
	public static function build(array $clocks)
	{
		$x = [];
		ksort($clocks, SORT_STRING);
		foreach ($clocks as $name => $c) {
			$x[$name] = [
				['NoOp', 'Speaking clock ' . $name],
				['SpeakingClock', self::appArgs($c)],
				['Hangup', ''],
			];
		}
		foreach ($clocks as $name => $c)
			if ($c['number'] !== '' && !isset($x[$c['number']]))
				$x[$c['number']] = [['Goto', self::CONTEXT . ',' . $name . ',1']];
		return $x;
	}

	/* whether the context has numbers to dial, so it is included in
	   from-internal-additional */
	public static function hasNumbers(array $clocks)
	{
		foreach ($clocks as $c)
			if ($c['number'] !== '')
				return true;
		return false;
	}

	/* the context as extensions.conf text */
	public static function render(array $context, $include = false)
	{
		$o = [];
		if ($include)
			$o[] = '[' . self::INCLUDE_IN . "]\ninclude => " . self::CONTEXT . "\n";
		$o[] = '[' . self::CONTEXT . ']';
		foreach ($context as $exten => $steps)
			foreach ($steps as $i => $st)
				$o[] = ($i === 0 ? 'exten => ' . $exten . ',1,' : ' same => n,') .
					$st[0] . '(' . $st[1] . ')';
		return implode("\n", $o) . "\n";
	}

	/* speakingclock.conf for settings s */
	public static function renderConf(array $s)
	{
		$o = "; speakingclock.conf - written by FreePBX's Speaking Clock module.\n" .
		     "; Edits here are lost: change Applications > Speaking Clock > Settings.\n\n" .
		     "[general]\n" .
		     'voicedir=' . $s['voicedir'] . "\n" .
		     'default_voice=' . $s['default_voice'] . "\n";
		if ($s['timezone'] !== '')
			$o .= 'timezone=' . $s['timezone'] . "\n";
		$o .= 'maxduration=' . (int)$s['maxduration'] . "\n";
		foreach (['tts_program_dectalk', 'tts_program_sam'] as $k)
			if ($s[$k] !== '')
				$o .= $k . '=' . $s[$k] . "\n";
		return $o;
	}
}
