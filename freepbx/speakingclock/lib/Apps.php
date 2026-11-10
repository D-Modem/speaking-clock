<?php
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 D-Modem Authors
//
// Apps.php - a dialplan step of any application for FreePBX's extension
// class (SpeakingClock(), Goto(), ...).  Loaded by doDialplanHook(), once
// FreePBX's extension class is loaded.

if (class_exists('extension', false) && !class_exists('ext_speakingclock_app', false)) {
	class ext_speakingclock_app extends extension
	{
		public $app;

		public function __construct($app, $data = '')
		{
			$this->app = $app;
			$this->data = $data;
		}

		public function output()
		{
			return $this->app . '(' . $this->data . ')';
		}
	}
}
