// Sandshell: Android SELinux forbids hard links for apps (link() -> EACCES). dsh uses link(tmp, final) as an
// atomic no-clobber publish. Fallback (same shape as the community dsh patch): on EACCES/EPERM for a regular
// file, refuse if the target exists (EEXIST), else copy to a private temp and rename() it into place.
// The result is an independent copy (nlink 1), not a shared inode. Load with NODE_OPTIONS="--require <this>".
'use strict';
const fs = require('fs'), fsp = fs.promises, path = require('path');
if (!fs.__ssLinkfix) {
  fs.__ssLinkfix = true;
  const oLink = fs.link, oLinkSync = fs.linkSync, oPLink = fsp.link;
  const TEST = process.env.SS_LINKFIX_TEST === '1';
  const deny = () => Object.assign(new Error('EACCES (test)'), { code: 'EACCES' });
  const want = e => e && (e.code === 'EACCES' || e.code === 'EPERM');
  const eexist = (s, d) => Object.assign(new Error(`EEXIST: file already exists, link '${s}' -> '${d}'`),
    { code: 'EEXIST', errno: -17, syscall: 'link', path: String(s), dest: String(d) });
  const tmpOf = d => path.join(path.dirname(String(d)), `.${path.basename(String(d))}.sslink.${process.pid}.${Math.random().toString(36).slice(2, 10)}`);
  function fbSync(s, d, err) {
    const st = fs.lstatSync(s); if (!st.isFile()) throw err;
    if (fs.existsSync(d) || (() => { try { fs.lstatSync(d); return true; } catch { return false; } })()) throw eexist(s, d);
    const t = tmpOf(d);
    try { fs.copyFileSync(s, t, fs.constants.COPYFILE_EXCL); fs.chmodSync(t, st.mode & 0o7777);
      try { fs.lstatSync(d); throw eexist(s, d); } catch (e) { if (e.code !== 'ENOENT') throw e; }
      fs.renameSync(t, d); }
    catch (e) { try { fs.unlinkSync(t); } catch {} throw e; }
  }
  async function fbAsync(s, d, err) {
    const st = await fsp.lstat(s); if (!st.isFile()) throw err;
    try { await fsp.lstat(d); throw eexist(s, d); } catch (e) { if (e.code !== 'ENOENT') throw e; }
    const t = tmpOf(d);
    try { await fsp.copyFile(s, t, fs.constants.COPYFILE_EXCL); await fsp.chmod(t, st.mode & 0o7777);
      try { await fsp.lstat(d); throw eexist(s, d); } catch (e) { if (e.code !== 'ENOENT') throw e; }
      await fsp.rename(t, d); }
    catch (e) { try { await fsp.unlink(t); } catch {} throw e; }
  }
  fs.linkSync = function (s, d) { try { if (TEST) throw deny(); return oLinkSync(s, d); } catch (e) { if (!want(e)) throw e; fbSync(s, d, e); } };
  fsp.link = async function (s, d) { try { if (TEST) throw deny(); return await oPLink(s, d); } catch (e) { if (!want(e)) throw e; await fbAsync(s, d, e); } };
  fs.link = function (s, d, cb) { fsp.link(s, d).then(() => cb(null), cb); };
  try { require('module').syncBuiltinESMExports(); } catch {}
}
