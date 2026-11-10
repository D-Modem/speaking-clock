<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// Speakingclock.class.php - the FreePBX module (BMO): clocks kept in the
// module's store, each a destination (app-speakingclock,<name>,1) and
// optionally a number to dial; speakingclock.conf written from the
// settings on Apply Config.

namespace FreePBX\modules;

require_once __DIR__ . '/lib/Clock.php';

use FreePBX\modules\Speakingclock\Clock;

class Speakingclock extends \FreePBX_Helpers implements \BMO
{
	const SETTINGS = 'settings';
	const CLOCKS = 'clocks';

	public $FreePBX;
	public $errors = [];
	public $posted = null;
	public $message = null;

	public function __construct($freepbx = null)
	{
		if ($freepbx === null)
			throw new \Exception('Not given a FreePBX object');
		$this->FreePBX = $freepbx;
	}

	public static function h($s)
	{
		return htmlspecialchars((string)$s, ENT_QUOTES, 'UTF-8');
	}

	/* the store keeps false as "delete" */
	private static function stored($v)
	{
		return is_bool($v) ? ($v ? 'yes' : 'no') : $v;
	}

	public function install()
	{
		$have = $this->getAll(self::SETTINGS);
		$def = Clock::defaultSettings();
		/* Asterisk's own sounds folder, where make install puts the voices */
		$lib = $this->astVarLibDir();
		if ($lib !== '')
			$def['voicedir'] = $lib . '/sounds/speaking-clock';
		foreach ($def as $k => $v)
			if (!is_array($have) || !array_key_exists($k, $have))
				$this->setConfig($k, self::stored($v), self::SETTINGS);
	}

	public function uninstall()
	{
		$this->delById(self::SETTINGS);
		$this->delById(self::CLOCKS);
	}

	private function astVarLibDir()
	{
		$c = isset($this->FreePBX->Config) ? $this->FreePBX->Config : null;
		if ($c && method_exists($c, 'get')) {
			$d = $c->get('ASTVARLIBDIR');
			if (is_string($d) && $d !== '')
				return rtrim($d, '/');
		}
		return '';
	}

	public function getSettings()
	{
		$have = $this->getAll(self::SETTINGS);
		return Clock::normalizeSettings(array_merge(Clock::defaultSettings(),
			is_array($have) ? $have : []));
	}

	public function saveSettings(array $s)
	{
		foreach ($s as $k => $v)
			$this->setConfig($k, self::stored($v), self::SETTINGS);
	}

	/* name => clock, by name */
	public function getClocks()
	{
		$all = $this->getAll(self::CLOCKS);
		$out = [];
		if (is_array($all))
			foreach ($all as $name => $c)
				if (is_array($c))
					$out[$name] = Clock::normalizeClock(array_merge(
						Clock::defaultClock(), $c, ['name' => $name]));
		ksort($out, SORT_STRING);
		return $out;
	}

	public function getClock($name)
	{
		$all = $this->getClocks();
		return isset($all[$name]) ? $all[$name] : null;
	}

	public function saveClock(array $c)
	{
		$this->setConfig($c['name'], $c, self::CLOCKS);
	}

	public function deleteClock($name)
	{
		$this->setConfig($name, false, self::CLOCKS);
	}

	/* id => voice info, from the voice folder */
	public function voices()
	{
		return Clock::voices($this->getSettings()['voicedir']);
	}

	public static function destination($name)
	{
		return Clock::CONTEXT . ',' . $name . ',1';
	}

	private function destinationUsers($dest)
	{
		if (!function_exists('framework_check_destination_usage'))
			return [];
		$u = framework_check_destination_usage($dest);
		return is_array($u) ? $u : [];
	}

	private function reload()
	{
		if (function_exists('needreload'))
			needreload();
	}

	/* the numbers clocks other than name have */
	private function takenNumbers($name)
	{
		$n = [];
		foreach ($this->getClocks() as $k => $c)
			if ($k !== $name && $c['number'] !== '')
				$n[] = $c['number'];
		return $n;
	}

	public function doConfigPageInit($page)
	{
		$req = $_REQUEST;
		$action = isset($req['action']) ? $req['action'] : '';
		if ($action === 'delete' && isset($req['clock'])) {
			$name = (string)$req['clock'];
			if ($this->getClock($name) === null)
				return;
			if ($this->destinationUsers(self::destination($name))) {
				$this->message = ['danger', sprintf(_('Clock %s is the destination of a route or another object: point those elsewhere first'), $name)];
				$_REQUEST['view'] = 'form';
				return;
			}
			$this->deleteClock($name);
			$this->reload();
			$this->message = ['success', sprintf(_('Clock %s deleted'), $name)];
			$_REQUEST['view'] = '';
			return;
		}
		if ($action === 'save_clock') {
			$c = Clock::normalizeClock($req);
			$old = isset($req['original']) ? (string)$req['original'] : '';
			if ($old !== '') {
				$c['name'] = $old;      /* a clock keeps its name: it is its destination */
				$taken = [];
			} else {
				$taken = array_keys($this->getClocks());
			}
			$this->errors = Clock::validateClock($c, $taken, $this->takenNumbers($c['name']));
			if (!$this->errors && !isset($this->voices()[$c['voice']]))
				$this->errors['voice'] = _('No such voice in the voice folder');
			if ($this->errors) {
				$this->posted = $c;
				$_REQUEST['view'] = 'form';
				return;
			}
			$this->saveClock($c);
			$this->reload();
			$this->message = ['success', sprintf(_('Clock %s saved'), $c['name'])];
			$_REQUEST['view'] = '';
			return;
		}
		if ($action === 'save_settings') {
			$s = Clock::normalizeSettings($req);
			$this->errors = Clock::validateSettings($s);
			if ($this->errors) {
				$this->posted = $s;
				$_REQUEST['view'] = 'settings';
				return;
			}
			$this->saveSettings($s);
			$this->reload();
			$this->message = ['success', _('Settings saved')];
			return;
		}
	}

	public function getActionBar($request)
	{
		if (!isset($request['display']) || $request['display'] !== 'speakingclock')
			return [];
		$view = isset($request['view']) ? $request['view'] : '';
		if ($view !== 'form' && $view !== 'settings')
			return [];
		$b = [
			'reset' => ['name' => 'reset', 'id' => 'reset', 'value' => _('Reset')],
			'submit' => ['name' => 'submit', 'id' => 'submit', 'value' => _('Submit')],
		];
		if ($view === 'form' && !empty($request['clock']))
			$b['delete'] = ['name' => 'delete', 'id' => 'delete', 'value' => _('Delete')];
		return $b;
	}

	public function view($view, array $vars = [])
	{
		if (!preg_match('/^[a-z_]+$/', $view))
			return '';
		$vars['sc'] = $this;
		extract($vars, EXTR_SKIP);
		ob_start();
		include __DIR__ . '/views/' . $view . '.php';
		return ob_get_clean();
	}

	public function showPage()
	{
		$view = isset($_REQUEST['view']) ? $_REQUEST['view'] : '';
		$settings = $this->getSettings();
		$voices = $this->voices();
		$common = ['view' => $view, 'message' => $this->message,
			   'settings' => $settings, 'voices' => $voices];
		switch ($view) {
		case 'form':
			$name = isset($_REQUEST['clock']) ? (string)$_REQUEST['clock'] : '';
			$c = $this->posted !== null ? $this->posted :
				($name !== '' ? $this->getClock($name) : null);
			$editing = $name !== '' && $this->getClock($name) !== null;
			if ($c === null) {
				$c = Clock::defaultClock();
				$c['voice'] = $settings['default_voice'];
			}
			$body = $this->view('clock', ['clock' => $c, 'editing' => $editing,
				'errors' => $this->errors, 'voices' => $voices]);
			break;
		case 'settings':
			$s = $this->posted !== null ? $this->posted : $settings;
			$body = $this->view('settings', ['s' => $s, 'errors' => $this->errors,
				'voices' => $voices]);
			break;
		case 'voices':
			$body = $this->view('voices', ['voices' => $voices,
				'dir' => $settings['voicedir']]);
			break;
		case 'dialplan':
			$clocks = $this->getClocks();
			$body = $this->view('dialplan', [
				'conf' => Clock::renderConf($settings),
				'dialplan' => Clock::render(Clock::build($clocks),
					Clock::hasNumbers($clocks))]);
			break;
		default:
			$common['view'] = '';
			$body = $this->view('clocks', ['clocks' => $this->getClocks(),
				'voices' => $voices]);
		}
		return $this->view('main', $common + ['body' => $body]);
	}

	public function genConfig()
	{
		return ['speakingclock.conf' => Clock::renderConf($this->getSettings())];
	}

	public function writeConfig($conf)
	{
		$this->FreePBX->WriteConfig($conf);
	}

	public static function myDialplanHooks()
	{
		return 500;
	}

	public function doDialplanHook(&$ext, $engine, $priority)
	{
		require_once __DIR__ . '/lib/Apps.php';
		$clocks = $this->getClocks();
		foreach (Clock::build($clocks) as $exten => $steps)
			foreach ($steps as $st)
				$ext->add(Clock::CONTEXT, $exten, '',
					  new \ext_speakingclock_app($st[0], $st[1]));
		if (Clock::hasNumbers($clocks))
			$ext->addInclude(Clock::INCLUDE_IN, Clock::CONTEXT);
	}

	public function destinations()
	{
		$d = [];
		foreach ($this->getClocks() as $name => $c)
			$d[] = [
				'destination' => self::destination($name),
				'description' => $c['description'] !== '' ?
					$name . ' (' . $c['description'] . ')' : $name,
				'category' => 'Speaking Clock',
			];
		return $d;
	}

	public function destinationInfo($dest)
	{
		$pre = Clock::CONTEXT . ',';
		if (strpos($dest, $pre) !== 0)
			return null;
		$parts = explode(',', $dest);
		$name = isset($parts[1]) ? $parts[1] : '';
		if ($this->getClock($name) === null)
			return [];
		return ['description' => sprintf(_('Speaking clock %s'), $name),
			'edit_url' => 'config.php?display=speakingclock&view=form&clock=' .
				urlencode($name)];
	}
}
