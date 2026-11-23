# FreePBX

Do one of these procedures:

- Put `freepbx/speakingclock` into a tarball and install it with Module
  Admin (upload).
- Copy `freepbx/speakingclock` to `/var/www/html/admin/modules/` and run
  `fwconsole ma install speakingclock`.

The module must have app_speakingclock, installed as [Asterisk](asterisk.md) tells.
Then **Applications > Speaking Clock** lists your clocks. Each clock is a
destination for inbound routes, IVRs and time conditions. Each clock can
also have a number that extensions can dial.
