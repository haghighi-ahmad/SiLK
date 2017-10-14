#######################################################################
# Copyright (C) 2009-2017 by Carnegie Mellon University.
#
# @OPENSOURCE_LICENSE_START@
# See license information in ../../../LICENSE.txt
# @OPENSOURCE_LICENSE_END@
#
#######################################################################

#######################################################################
# $SiLK: gencerts.py 275df62a2e41 2017-01-05 17:30:40Z mthomas $
#######################################################################

import sys
import os
import os.path
import re
import subprocess

top_builddir = os.environ.get('top_builddir')
if top_builddir:
    sys.path.insert(0, os.path.join(top_builddir, "tests"))
from config_vars import config_vars


key_bits = 1024

tls_template = """
organization = "test"
unit = "test"
state = "test"
country = US
cn = "test"
serial = 001
expiration_days = 18300
"""

tls_ca_template = tls_template + """
ca
signing_key
cert_signing_key
"""

tls_prog_template = tls_template + """
signing_key
encryption_key
"""

tls_p12_template = """
pkcs12_key_name = "test"
password = ""
"""

r = re.compile(r'key[0-9]+\.pem$')
srcdir = os.environ.get("srcdir")
if srcdir is None:
    srcdir = os.path.join(config_vars["abs_srcdir"],
                          "..", "src", "sendrcv")
keyloc = os.path.join(srcdir, "tests")
potentials = [os.path.join(keyloc, x) for x in os.listdir(keyloc)
              if r.match(x)]
potentials_used = []

r = re.compile(r'ca_cert_(key[0-9]+\.pem)$')
ca_certs = []
ca_certs_used = []
for f in os.listdir(keyloc):
    match = r.match(f)
    if match:
        key = os.path.join(keyloc, match.group(1))
        try:
            potentials.remove(key)
        except ValueError:
            pass
        ca_certs.append((key, os.path.join(keyloc, f)))

r = re.compile(r'cert-([^-]+)-(.+)\.p12$')
p12_map = {}
for f in os.listdir(keyloc):
    match = r.match(f)
    if match:
        p12 = os.path.join(keyloc, f)
        ca = match.group(2)
        if ca in p12_map:
            p12_map[ca].append(p12)
        else:
            p12_map[ca] = [p12]
p12s_used = []


def check_call(*popenargs, **kwargs):
    sys.stderr.write('Calling "%s"\n' % ' '.join(*popenargs))
    if call(*popenargs, **kwargs):
        raise RuntimeError("Failed to execute %s" % ' '.join(*popenargs))


def check_certtool():
    try:
        proc = subprocess.Popen(['certtool', '--version'], close_fds=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        output = '\n'.join(proc.stdout)
        if not re.match(r"(?ism)certtool\b.*\b(lib)?gnutls\b", output):
            raise RuntimeError
    except OSError as RuntimeError:
        raise RuntimeError("Cannot find a valid certtool")


def create_certs(tmpdir):
    ca_cert_pair = generate_ca_cert(tmpdir, "ca_cert.pem")
    newname = "ca_cert_" + os.path.basename(ca_cert_pair[0])
    os.rename(ca_cert_pair[1], newname)
    ca_cert_pair = (ca_cert_pair[0], newname)
    key = get_key()
    while key:
        key_base = os.path.splitext(os.path.basename(key))[0]
        ca_base = os.path.splitext(os.path.basename(ca_cert_pair[1]))[0]
        newname = "cert-" + key_base + "-" + ca_base + ".p12"
        generate_signed_cert(tmpdir, ca_cert_pair, key, newname)
        key = get_key()

def reset_used_keys():
    global potentials
    global potentials_used
    potentials.extend(potentials_used)
    used = []

def get_key():
    global potentials
    global potentials_used
    if potentials:
        key = potentials.pop()
        potentials_used.append(key)
        return key
    return None

def generate_key(filename):
    name = get_key()
    if not name:
        name = filename
        check_certtool()
        check_call(
            ['certtool', '--generate-privkey', '--outfile', filename,
             '--bits', str(key_bits)])
    return name

def get_ca_cert():
    global ca_certs
    global ca_certs_used
    if ca_certs:
        cert = ca_certs.pop()
        ca_certs_used.append(cert)
        return cert

def reset_ca_certs():
    global ca_certs
    global ca_certs_used
    ca_certs.extend(ca_certs_used)
    ca_certs_used = []


def create_self_signed_ca_cert(tmpdir, key, filename):
    template = os.path.join(tmpdir, "template")
    template_file = open(template, "w")
    template_file.write(tls_ca_template)
    template_file.close()
    try:
        check_certtool()
        check_call(
            ['certtool', '--generate-self-signed', '--template',
             template, '--load-privkey', key, '--outfile', filename])
    finally:
        os.unlink(template)

def generate_ca_cert(tmpdir, filename):
    ca_cert = get_ca_cert()
    if not ca_cert:
        key = generate_key(os.path.join(tmpdir, "ca_key.pem"))
        ca_cert = (key, filename)
        create_self_signed_ca_cert(tmpdir, key, filename)
    return ca_cert


def create_signed_cert(tmpdir, ca_cert_pair, key, filename):
    (ca_key, ca_cert) = ca_cert_pair
    template = os.path.join(tmpdir, "template")
    template_file = open(template, "w")
    template_file.write(tls_prog_template)
    template_file.close()
    template12 = os.path.join(tmpdir, "template12")
    template12_file = open(template12, "w")
    template12_file.write(tls_p12_template)
    template12_file.close()
    try:
        check_certtool()
        check_call(
            ['certtool', '--generate-request', '--load-privkey', key,
             '--outfile', os.path.join(tmpdir, 'request.pem'),
             '--template', template])
        check_call(
            ['certtool', '--generate-certificate',
             '--load-request', os.path.join(tmpdir, 'request.pem'),
             '--outfile', os.path.join(tmpdir, 'cert.pem'),
             '--template', template,
             '--load-ca-certificate', ca_cert,
             '--load-ca-privkey', ca_key])
        check_call(
            ['certtool',
             '--load-certificate', os.path.join(tmpdir, 'cert.pem'),
             '--load-privkey', key,
             '--to-p12', '--outder', '--template', template12,
             '--outfile', filename])
    finally:
        os.unlink(template)
        os.unlink(template12)

def generate_signed_cert(tmpdir, ca_cert_pair, key, filename):
    global p12_map
    global p12s_used
    (ca_key, ca_cert) = ca_cert_pair
    basecert = os.path.splitext(os.path.basename(ca_cert))[0]
    if basecert in p12_map:
        plist = p12_map[basecert]
        p12 = plist.pop()
        p12s_used.append((basecert, p12))
        return p12
    if not os.path.exists(key):
        key = generate_key(key)
    create_signed_cert(tmpdir, ca_cert_pair, key, filename)
    return filename

def reset_used_certs():
    global p12_map
    global p12s_used
    for key, value in p12s_used:
        p12_map[key].append(value)
    p12s_used = []

def reset_all_certs_and_keys():
    reset_used_keys()
    reset_ca_certs()
    reset_used_certs()

if __name__ == '__main__':
    if not potentials:
        for i in range(1,9):
            generate_key("key%s.pem" % i)
    else:
        create_certs("/tmp")
