<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// unit.php - the FreePBX module's tests, without FreePBX: a small stand-in
// for what the module uses of it (the kvstore, needreload(), the extension
// classes), then the forms, the store, the voices, speakingclock.conf, the
// dialplan, the destinations and the pages.  php freepbx/test/unit.php;
// exit status 0 if all pass.

error_reporting(E_ALL);
set_error_handler(function ($no, $str, $file, $line) {
	throw new ErrorException($str, 0, $no, $file, $line);
});
define('FREEPBX_IS_AUTH', true);
if (!function_exists('_')) {
	function _($s) { return $s; }
}

interface BMO
{
	public function install();
	public function uninstall();
}

/* DB_Helper's kvstore in memory: false deletes, arrays kept as given */
class FreePBX_Helpers
{
	public static $kv = [];
	public function setConfig($key, $val = false, $id = 'noid')
	{
		if ($val === false)
			unset(self::$kv[$id][$key]);
		else
			self::$kv[$id][$key] = is_array($val) ? $val : (string)$val;
	}
	public function getAll($id = 'noid')
	{
		return isset(self::$kv[$id]) ? self::$kv[$id] : [];
	}
	public function delById($id)
	{
		unset(self::$kv[$id]);
	}
}

$reloads = 0;
function needreload() { global $reloads; $reloads++; }
$destUsers = [];
function framework_check_destination_usage($d)
{
	global $destUsers;
	return isset($destUsers[$d]) ? $destUsers[$d] : [];
}

/* FreePBX's extensions class, as much as doDialplanHook() uses */
class extension
{
	public $data;
	public function output() { return ''; }
}
class extensions
{
	public $ctx = [];
	public $inc = [];
	public function add($context, $exten, $label, $cmd)
	{
		$this->ctx[$context][$exten][] = $cmd->output();
	}
	public function addInclude($context, $include)
	{
		$this->inc[$context][] = $include;
	}
}

class FakeConfig
{
	public function get($k) { return $k === 'ASTVARLIBDIR' ? '/var/lib/asterisk' : null; }
}

$root = dirname(__DIR__) . '/speakingclock';
require_once $root . '/Speakingclock.class.php';

use FreePBX\modules\Speakingclock;
use FreePBX\modules\Speakingclock\Clock;

$failed = 0;
$passed = 0;
function ok($cond, $what)
{
	global $failed, $passed;
	if ($cond) {
		$passed++;
	} else {
		$failed++;
		echo "FAIL: $what\n";
	}
}

$voicedir = realpath(__DIR__ . '/../../voices');
$fpbx = (object)['Config' => new FakeConfig(), 'written' => null];
$sc = new Speakingclock($fpbx);

/* install: defaults, voicedir from ASTVARLIBDIR */
$sc->install();
$s = $sc->getSettings();
ok($s['voicedir'] === '/var/lib/asterisk/sounds/speaking-clock', 'install sets voicedir from ASTVARLIBDIR');
ok($s['default_voice'] === 'tim-pat-simmons', 'default voice');
$sc->saveSettings(array_merge($s, ['voicedir' => $voicedir]));
ok($sc->getSettings()['voicedir'] === $voicedir, 'settings kept');

/* voices from the repository's voices/ */
$v = $sc->voices();
ok(count($v) >= 80, 'voices found: ' . count($v));
ok(isset($v['tim-pat-simmons']) && $v['tim-pat-simmons']['mode'] === 'tim', 'Pat Simmons is a tim voice');
ok(isset($v['big-ben']) && $v['big-ben']['mode'] === 'chime', 'Big Ben chimes');
ok(isset($v['atandt-audrey']) && $v['atandt-audrey']['name'] === 'AT&T Audrey', 'names with & read');
ok(!isset($v['INDEX.md']), 'files are not voices');

/* settings checks */
ok(!Clock::validateSettings(Clock::normalizeSettings(['voicedir' => '/x', 'default_voice' => 'a', 'timezone' => 'Europe/London', 'maxduration' => '0'])), 'good settings pass');
$e = Clock::validateSettings(Clock::normalizeSettings(['voicedir' => 'rel', 'default_voice' => '../x', 'timezone' => 'Mars/Base', 'maxduration' => '-1']));
ok(isset($e['voicedir'], $e['default_voice'], $e['timezone'], $e['maxduration']), 'bad settings caught');
$e = Clock::validateSettings(Clock::normalizeSettings(['voicedir' => '/x', 'default_voice' => 'a', 'tts_program_sam' => 'sam; rm -rf /']));
ok(isset($e['tts_program_sam']), 'bad tts_program caught');
$t = Clock::normalizeSettings(['voicedir' => '/x', 'default_voice' => 'a', 'tts_program_dectalk' => '/opt/dectalk/say']);
ok(!Clock::validateSettings($t) && strpos(Clock::renderConf($t), "tts_program_dectalk=/opt/dectalk/say\n") !== false, 'dectalk program written');

/* clock checks */
$good = Clock::normalizeClock(['name' => 'tim', 'voice' => 'tim-pat-simmons', 'number' => '123',
	'timezone' => 'Europe/London', 'period' => '0', 'duration' => '']);
ok(!Clock::validateClock($good), 'good clock passes');
foreach ([
	['name' => '1abc'], ['name' => 'Bad'], ['name' => str_repeat('a', 33)],
	['voice' => '../../etc'], ['voice' => ''], ['number' => '12a'],
	['timezone' => 'Nowhere'], ['period' => 'x'], ['period' => '9999'],
	['duration' => '-5'], ['description' => "two\nlines"],
] as $bad) {
	$c = array_merge($good, $bad);
	ok(Clock::validateClock($c) !== [], 'caught bad ' . json_encode($bad));
}
ok(isset(Clock::validateClock($good, ['tim'])['name']), 'name taken');
ok(isset(Clock::validateClock($good, [], ['123'])['number']), 'number taken');
ok(Clock::normalizeClock(['hours24' => 'yes', 'wait' => 'on'])['hours24'] === true, 'checkbox yes');
ok(Clock::normalizeClock([])['wait'] === false, 'checkbox absent is no');

/* SpeakingClock() arguments */
ok(Clock::appArgs($good) === 'tim-pat-simmons,t(Europe/London)', 'app args: zone');
ok(Clock::appArgs(array_merge($good, ['timezone' => '', 'duration' => '300', 'period' => '15',
	'hours24' => true, 'wait' => true, 'exit_key' => true])) === 'tim-pat-simmons,d(300)p(15)hwx', 'app args: all options');
ok(Clock::appArgs(array_merge($good, ['timezone' => ''])) === 'tim-pat-simmons', 'app args: none');

/* the page's forms */
$_REQUEST = ['action' => 'save_clock', 'name' => 'tim', 'voice' => 'tim-pat-simmons',
	'number' => '123', 'timezone' => 'Europe/London', 'description' => 'UK TIM'];
$sc->doConfigPageInit('speakingclock');
ok(!$sc->errors && $sc->getClock('tim') !== null, 'clock saved: ' . json_encode($sc->errors));
ok($reloads === 1, 'Apply Config asked for');
$sc->errors = [];
$_REQUEST = ['action' => 'save_clock', 'name' => 'oz', 'voice' => 'no-such-voice'];
$sc->doConfigPageInit('speakingclock');
ok(isset($sc->errors['voice']) && $sc->getClock('oz') === null, 'unknown voice refused');
$sc->errors = [];
$_REQUEST = ['action' => 'save_clock', 'name' => 'oz', 'voice' => 'tim-gordon-gow', 'number' => '123'];
$sc->doConfigPageInit('speakingclock');
ok(isset($sc->errors['number']), 'second clock on the same number refused');
$sc->errors = [];
$_REQUEST = ['action' => 'save_clock', 'name' => 'oz', 'voice' => 'tim-gordon-gow',
	'number' => '1194', 'timezone' => 'Australia/Sydney'];
$sc->doConfigPageInit('speakingclock');
ok($sc->getClock('oz') !== null, 'second clock saved');
$sc->errors = [];
$_REQUEST = ['action' => 'save_clock', 'original' => 'oz', 'name' => 'renamed', 'voice' => 'tim-gordon-gow',
	'number' => '1194', 'timezone' => 'Australia/Sydney', 'description' => 'Gordon'];
$sc->doConfigPageInit('speakingclock');
ok($sc->getClock('oz')['description'] === 'Gordon' && $sc->getClock('renamed') === null, 'edit keeps the name');
$_REQUEST = ['action' => 'save_clock', 'name' => 'ben', 'voice' => 'big-ben'];
$sc->doConfigPageInit('speakingclock');

/* the dialplan */
$ext = new extensions();
$sc->doDialplanHook($ext, 'asterisk', 500);
$ctx = $ext->ctx[Clock::CONTEXT];
ok($ctx['tim'] === ['NoOp(Speaking clock tim)', 'SpeakingClock(tim-pat-simmons,t(Europe/London))', 'Hangup()'], 'tim extension');
ok($ctx['123'] === ['Goto(app-speakingclock,tim,1)'], 'number 123 goes to tim');
ok($ctx['1194'] === ['Goto(app-speakingclock,oz,1)'], 'number 1194 goes to oz');
ok($ctx['ben'][1] === 'SpeakingClock(big-ben)', 'ben extension');
ok($ext->inc[Clock::INCLUDE_IN] === [Clock::CONTEXT], 'included in from-internal-additional');
$txt = Clock::render(Clock::build($sc->getClocks()), true);
ok(strpos($txt, "[from-internal-additional]\ninclude => app-speakingclock") === 0, 'render: include');
ok(strpos($txt, "exten => tim,1,NoOp(Speaking clock tim)\n same => n,SpeakingClock(") !== false, 'render: steps');

/* speakingclock.conf */
$conf = $sc->genConfig()['speakingclock.conf'];
ok(strpos($conf, "[general]\nvoicedir=$voicedir\ndefault_voice=tim-pat-simmons\nmaxduration=180\n") !== false, 'speakingclock.conf: ' . $conf);

/* destinations */
$d = $sc->destinations();
ok(count($d) === 3 && $d[2]['destination'] === 'app-speakingclock,tim,1' && $d[2]['description'] === 'tim (UK TIM)', 'destinations');
ok($sc->destinationInfo('app-speakingclock,tim,1')['edit_url'] === 'config.php?display=speakingclock&view=form&clock=tim', 'destination info');
ok($sc->destinationInfo('app-other,x,1') === null, 'not our destination');
ok($sc->destinationInfo('app-speakingclock,gone,1') === [], 'a deleted clock');

/* delete: refused while something points at it */
$destUsers['app-speakingclock,tim,1'] = ['ivr' => 'x'];
$_REQUEST = ['action' => 'delete', 'clock' => 'tim'];
$sc->doConfigPageInit('speakingclock');
ok($sc->getClock('tim') !== null && $sc->message[0] === 'danger', 'delete refused while in use');
$destUsers = [];
$sc->doConfigPageInit('speakingclock');
ok($sc->getClock('tim') === null, 'deleted');

/* the pages render */
foreach (['', 'form', 'settings', 'voices', 'dialplan'] as $view) {
	$_REQUEST = ['view' => $view, 'clock' => $view === 'form' ? 'oz' : ''];
	$sc->message = null;
	$sc->posted = null;
	$sc->errors = [];
	$html = $sc->showPage();
	ok(strpos($html, 'id="speakingclock"') !== false, "page '$view' renders");
}
$_REQUEST = ['view' => 'form', 'clock' => 'oz'];
ok(strpos($sc->showPage(), 'value="1194"') !== false, 'form shows the number');
ok(strpos($sc->showPage(), 'Australia/Sydney" selected') !== false, 'form shows the zone');
ok(isset($sc->getActionBar(['display' => 'speakingclock', 'view' => 'form', 'clock' => 'oz'])['delete']), 'action bar');

/* uninstall empties the store */
$sc->uninstall();
ok($sc->getClocks() === [], 'uninstall');

echo "$passed passed, $failed failed\n";
exit($failed ? 1 : 0);
