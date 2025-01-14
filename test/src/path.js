import { test } from 'socket:test'
import process from 'socket:process'
import path from 'socket:path'
import os from 'socket:os'

test('path', (t) => {
  const isUnix = os.platform() !== 'win32'
  t.ok(path.posix, 'path.posix exports')
  t.ok(path.win32, 'path.win32 exports')
  const expectedSep = isUnix ? '/' : '\\'
  t.equal(path.sep, expectedSep, 'path.sep is correct')
  const expectedDelimiter = isUnix ? ':' : ';'
  t.equal(path.delimiter, expectedDelimiter, 'path.delimiter is correct')
  t.equal(path.cwd(), process.cwd(), 'path.cwd() returns the current working directory')
})
