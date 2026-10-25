// Sandshell: Android SELinux forbids link(2) for apps (EACCES). dsh uses link(tmp, final) as an
// atomic no-clobber publish. Fallback ONLY for this process tree: copy src to a hidden temp beside dst,
// then publish it with rename() if dst does not exist (EEXIST otherwise). src is left untouched, like link.
'use strict';
const fs = require('fs'), path = require('path'), { syncBuiltinESMExports } = require('module');
const FALL = new Set(['EACCES', 'EPERM', 'ENOSYS', 'ENOTSUP', 'EXDEV']);
const force = !!process.env.SANDSHELL_LINK_FORCE_FALLBACK; // tests
const eexist = (src, dst) => Object.assign(new Error(`EEXIST: file already exists, link '${src}' -> '${dst}'`), { code: 'EEXIST', errno: -17, syscall: 'link', path: String(src), dest: String(dst) });
const tmpOf = d => path.join(path.dirname(String(d)), `.${path.basename(String(d))}.sslink-${process.pid}-${Math.random().toString(36).slice(2)}`);
const oSync = fs.linkSync, oCb = fs.link, oP = fs.promises.link;
function fbSync(src, dst) {
  if (fs.existsSync(dst) || isLink(dst)) throw eexist(src, dst);
  const t = tmpOf(dst);
  try { fs.copyFileSync(src, t, fs.constants.COPYFILE_EXCL); fs.chmodSync(t, fs.statSync(src).mode & 0o7777);
        if (fs.existsSync(dst) || isLink(dst)) throw eexist(src, dst); fs.renameSync(t, dst); }
  catch (e) { try { fs.unlinkSync(t); } catch {} throw e; }
}
function isLink(p) { try { fs.lstatSync(p); return true; } catch { return false; } }
fs.linkSync = function linkSync(src, dst) {
  if (!force) { try { return oSync.call(fs, src, dst); } catch (e) { if (!FALL.has(e.code)) throw e; } }
  return fbSync(src, dst);
};
fs.promises.link = async function link(src, dst) {
  if (!force) { try { return await oP.call(fs.promises, src, dst); } catch (e) { if (!FALL.has(e.code)) throw e; } }
  return fbSync(src, dst);
};
fs.link = function link(src, dst, cb) {
  fs.promises.link(src, dst).then(() => cb && cb(null), e => cb && cb(e));
};
try { syncBuiltinESMExports(); } catch {}
