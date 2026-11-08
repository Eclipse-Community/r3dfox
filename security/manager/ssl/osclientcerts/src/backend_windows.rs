/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(non_camel_case_types)]

use pkcs11_bindings::*;
use rsclientcerts::cryptoki::*;
use rsclientcerts::manager::{ClientCertsBackend, CryptokiObject, Sign};
use rsclientcerts_util::*;
use rsclientcerts_util::error::{Error, ErrorType};
use std::convert::TryInto;
use std::ffi::{c_void, CStr, CString};
use std::ops::Deref;
use std::slice;
use std::time::{Duration, Instant};
use winapi::shared::bcrypt::*;
use winapi::shared::minwindef::{DWORD, PBYTE};
use winapi::um::errhandlingapi::GetLastError;
use winapi::um::ncrypt::*;
use winapi::um::wincrypt::{HCRYPTHASH, HCRYPTPROV, *};
use xpcom::interfaces::nsIEventTarget;
use xpcom::{RefPtr, XpCom};

// winapi has some support for ncrypt.h, but not for this function.
extern "system" {
    fn NCryptSignHash(
        hKey: NCRYPT_KEY_HANDLE,
        pPaddingInfo: *mut c_void,
        pbHashValue: PBYTE,
        cbHashValue: DWORD,
        pbSignature: PBYTE,
        cbSignature: DWORD,
        pcbResult: *mut DWORD,
        dwFlags: DWORD,
    ) -> SECURITY_STATUS;
}

/// Given a `CERT_INFO`, tries to return the bytes of the subject distinguished name as formatted by
/// `CertNameToStrA` using the flag `CERT_SIMPLE_NAME_STR`. This is used as the label for the
/// certificate.
fn get_cert_subject_dn(cert_info: &CERT_INFO) -> Result<Vec<u8>, Error> {
    let mut cert_info_subject = cert_info.Subject;
    let subject_dn_len = unsafe {
        CertNameToStrA(
            X509_ASN_ENCODING,
            &mut cert_info_subject,
            CERT_SIMPLE_NAME_STR,
            std::ptr::null_mut(),
            0,
        )
    };
    // subject_dn_len includes the terminating null byte.
    let mut subject_dn_string_bytes: Vec<u8> = vec![0; subject_dn_len as usize];
    let subject_dn_len = unsafe {
        CertNameToStrA(
            X509_ASN_ENCODING,
            &mut cert_info_subject,
            CERT_SIMPLE_NAME_STR,
            subject_dn_string_bytes.as_mut_ptr() as *mut i8,
            subject_dn_string_bytes
                .len()
                .try_into()
                .map_err(|_| error_here!(ErrorType::ValueTooLarge))?,
        )
    };
    if subject_dn_len as usize != subject_dn_string_bytes.len() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    Ok(subject_dn_string_bytes)
}

fn new_cert(cert: PCCERT_CONTEXT) -> Result<CryptokiCert, Error> {
    let der =
        unsafe { slice::from_raw_parts((*cert).pbCertEncoded, (*cert).cbCertEncoded as usize) };
    let cert_info = unsafe { &*(*cert).pCertInfo };
    let label = get_cert_subject_dn(cert_info)?;
    CryptokiCert::new(der.to_vec(), label)
}

struct CertContext(PCCERT_CONTEXT);

impl CertContext {
    fn new(cert: PCCERT_CONTEXT) -> CertContext {
        CertContext(unsafe { CertDuplicateCertificateContext(cert) })
    }
}

impl Drop for CertContext {
    fn drop(&mut self) {
        unsafe {
            CertFreeCertificateContext(self.0);
        }
    }
}

impl Deref for CertContext {
    type Target = PCCERT_CONTEXT;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// Safety: strictly speaking, it isn't safe to send `CertContext` across threads. The
/// implementation handles this by wrapping `CertContext` in `ThreadSpecificHandles`. However, in
/// order to implement `Drop` for `ThreadSpecificHandles`, the `CertContext` it holds must be sent
/// to the appropriate thread, hence this impl.
unsafe impl Send for CertContext {}

enum KeyHandle {
    NCrypt(NCRYPT_KEY_HANDLE),
    CryptoAPI(HCRYPTPROV, DWORD),
}

impl KeyHandle {
    fn from_cert(cert: &CertContext) -> Result<KeyHandle, Error> {
        let mut key_handle = 0;
        let mut key_spec = 0;
        let mut must_free = 0;
        unsafe {
            if CryptAcquireCertificatePrivateKey(
                **cert,
                0,
                std::ptr::null_mut(),
                &mut key_handle,
                &mut key_spec,
                &mut must_free,
            ) != 1
            {
                return Err(error_here!(
                    ErrorType::ExternalError,
                    GetLastError().to_string()
                ));
            }
        }
        if must_free == 0 {
            return Err(error_here!(ErrorType::ExternalError));
        }
        if key_spec == CERT_NCRYPT_KEY_SPEC {
            Ok(KeyHandle::NCrypt(key_handle as NCRYPT_KEY_HANDLE))
        } else {
            Ok(KeyHandle::CryptoAPI(key_handle as HCRYPTPROV, key_spec))
        }
    }

    fn sign(
        &self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
        do_signature: bool,
        key_type: KeyType,
    ) -> Result<Vec<u8>, Error> {
        match &self {
            KeyHandle::NCrypt(ncrypt_handle) => {
                Err(error_here!(ErrorType::LibraryFailure))//sign_ncrypt(ncrypt_handle, data, params, do_signature, key_type)
            }
            KeyHandle::CryptoAPI(hcryptprov, key_spec) => {
                sign_cryptoapi(hcryptprov, key_spec, data, params, do_signature)
            }
        }
    }
}

impl Drop for KeyHandle {
    fn drop(&mut self) {
        match self {
            KeyHandle::NCrypt(ncrypt_handle) => unsafe {
//              let _ = NCryptFreeObject(*ncrypt_handle);
            },
            KeyHandle::CryptoAPI(hcryptprov, _) => unsafe {
                let _ = CryptReleaseContext(*hcryptprov, 0);
            },
        }
    }
}

/// Safety: see the comment for the `Send` impl for `CertContext`.
unsafe impl Send for KeyHandle {}

fn sign_ncrypt(
    ncrypt_handle: &NCRYPT_KEY_HANDLE,
    data: &[u8],
    params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    do_signature: bool,
    key_type: KeyType,
) -> Result<Vec<u8>, Error> {
    let mut sign_params = SignParams::new(key_type, params)?;
    let params_ptr = sign_params.params_ptr();
    let flags = sign_params.flags();
    let mut data = data.to_vec();
    let mut signature_len = 0;
    // We call NCryptSignHash twice: the first time to get the size of the buffer we need to
    // allocate and then again to actually sign the data, if `do_signature` is `true`.
    let status = unsafe {
        NCryptSignHash(
            *ncrypt_handle,
            params_ptr,
            data.as_mut_ptr(),
            data.len()
                .try_into()
                .map_err(|_| error_here!(ErrorType::ValueTooLarge))?,
            std::ptr::null_mut(),
            0,
            &mut signature_len,
            flags,
        )
    };
    // 0 is "ERROR_SUCCESS" (but "ERROR_SUCCESS" is unsigned, whereas SECURITY_STATUS is signed)
    if status != 0 {
        return Err(error_here!(ErrorType::ExternalError, status.to_string()));
    }
    let mut signature = vec![0; signature_len as usize];
    if !do_signature {
        return Ok(signature);
    }
    let mut final_signature_len = signature_len;
    let status = unsafe {
        NCryptSignHash(
            *ncrypt_handle,
            params_ptr,
            data.as_mut_ptr(),
            data.len()
                .try_into()
                .map_err(|_| error_here!(ErrorType::ValueTooLarge))?,
            signature.as_mut_ptr(),
            signature_len,
            &mut final_signature_len,
            flags,
        )
    };
    if status != 0 {
        return Err(error_here!(ErrorType::ExternalError, status.to_string()));
    }
    if final_signature_len != signature_len {
        return Err(error_here!(ErrorType::ExternalError));
    }
    Ok(signature)
}

fn sign_cryptoapi(
    hcryptprov: &HCRYPTPROV,
    key_spec: &DWORD,
    data: &[u8],
    params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    do_signature: bool,
) -> Result<Vec<u8>, Error> {
    if params.is_some() {
        return Err(error_here!(ErrorType::LibraryFailure));
    }
    // data will be an encoded DigestInfo, which specifies the hash algorithm and bytes of the hash
    // to sign. However, CryptoAPI requires directly specifying the bytes of the hash, so it must
    // be extracted first.
    let (_, hash_bytes) = read_digest_info(data)?;
    let hash = HCryptHash::new(hcryptprov, hash_bytes)?;
    let mut signature_len = 0;
    if unsafe {
        CryptSignHashW(
            *hash,
            *key_spec,
            std::ptr::null_mut(),
            0,
            std::ptr::null_mut(),
            &mut signature_len,
        )
    } != 1
    {
        return Err(error_here!(
            ErrorType::ExternalError,
            unsafe { GetLastError() }.to_string()
        ));
    }
    let mut signature = vec![0; signature_len as usize];
    if !do_signature {
        return Ok(signature);
    }
    let mut final_signature_len = signature_len;
    if unsafe {
        CryptSignHashW(
            *hash,
            *key_spec,
            std::ptr::null_mut(),
            0,
            signature.as_mut_ptr(),
            &mut final_signature_len,
        )
    } != 1
    {
        return Err(error_here!(
            ErrorType::ExternalError,
            unsafe { GetLastError() }.to_string()
        ));
    }
    if final_signature_len != signature_len {
        return Err(error_here!(ErrorType::ExternalError));
    }
    // CryptoAPI returns the signature with the most significant byte last (little-endian),
    // whereas PKCS#11 expects the most significant byte first (big-endian).
    signature.reverse();
    Ok(signature)
}

struct HCryptHash(HCRYPTHASH);

impl HCryptHash {
    fn new(hcryptprov: &HCRYPTPROV, hash_bytes: &[u8]) -> Result<HCryptHash, Error> {
        let alg = match hash_bytes.len() {
            20 => CALG_SHA1,
            32 => CALG_SHA_256,
            48 => CALG_SHA_384,
            64 => CALG_SHA_512,
            _ => {
                return Err(error_here!(ErrorType::UnsupportedInput));
            }
        };
        let mut hash: HCRYPTHASH = 0;
        if unsafe { CryptCreateHash(*hcryptprov, alg, 0, 0, &mut hash) } != 1 {
            return Err(error_here!(
                ErrorType::ExternalError,
                unsafe { GetLastError() }.to_string()
            ));
        }
        if unsafe { CryptSetHashParam(hash, HP_HASHVAL, hash_bytes.as_ptr(), 0) } != 1 {
            return Err(error_here!(
                ErrorType::ExternalError,
                unsafe { GetLastError() }.to_string()
            ));
        }
        Ok(HCryptHash(hash))
    }
}

impl Drop for HCryptHash {
    fn drop(&mut self) {
        unsafe {
            CryptDestroyHash(self.0);
        }
    }
}

impl Deref for HCryptHash {
    type Target = HCRYPTHASH;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

// In some cases, the ncrypt API takes a pointer to a null-terminated wide-character string as a way
// of specifying an algorithm. The "right" way to do this would be to take the corresponding
// &'static str constant provided by the winapi crate, create an OsString from it, encode it as wide
// characters, and collect it into a Vec<u16>. However, since the implementation that provides this
// functionality isn't constant, we would have to manage the memory this creates and uses. Since
// rust structures generally can't be self-referrential, this memory would have to live elsewhere,
// and the nice abstractions we've created for this implementation start to break down. It's much
// simpler to hard-code the identifiers we support, since there are only four of them.
// The following arrays represent the identifiers "SHA1", "SHA256", "SHA384", and "SHA512",
// respectively.
const SHA1_ALGORITHM_STRING: &[u16] = &[83, 72, 65, 49, 0];
const SHA256_ALGORITHM_STRING: &[u16] = &[83, 72, 65, 50, 53, 54, 0];
const SHA384_ALGORITHM_STRING: &[u16] = &[83, 72, 65, 51, 56, 52, 0];
const SHA512_ALGORITHM_STRING: &[u16] = &[83, 72, 65, 53, 49, 50, 0];

enum SignParams {
    EC,
    RSA_PKCS1(BCRYPT_PKCS1_PADDING_INFO),
    RSA_PSS(BCRYPT_PSS_PADDING_INFO),
}

impl SignParams {
    fn new(
        key_type: KeyType,
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<SignParams, Error> {
        // EC is easy, so handle that first.
        match key_type {
            KeyType::EC(_) => return Ok(SignParams::EC),
            KeyType::RSA => {}
        }
        // If `params` is `Some`, we're doing RSA-PSS. If it is `None`, we're doing RSA-PKCS1.
        let pss_params = match params {
            Some(pss_params) => pss_params,
            None => {
                // The hash algorithm should be encoded in the data to be signed, so we don't have to
                // (and don't want to) specify a particular algorithm here.
                return Ok(SignParams::RSA_PKCS1(BCRYPT_PKCS1_PADDING_INFO {
                    pszAlgId: std::ptr::null(),
                }));
            }
        };
        let algorithm_string = match pss_params.hashAlg {
            CKM_SHA_1 => SHA1_ALGORITHM_STRING,
            CKM_SHA256 => SHA256_ALGORITHM_STRING,
            CKM_SHA384 => SHA384_ALGORITHM_STRING,
            CKM_SHA512 => SHA512_ALGORITHM_STRING,
            _ => {
                return Err(error_here!(ErrorType::UnsupportedInput));
            }
        };
        Ok(SignParams::RSA_PSS(BCRYPT_PSS_PADDING_INFO {
            pszAlgId: algorithm_string.as_ptr(),
            cbSalt: pss_params.sLen,
        }))
    }

    fn params_ptr(&mut self) -> *mut std::ffi::c_void {
        match self {
            SignParams::EC => std::ptr::null_mut(),
            SignParams::RSA_PKCS1(params) => {
                params as *mut BCRYPT_PKCS1_PADDING_INFO as *mut std::ffi::c_void
            }
            SignParams::RSA_PSS(params) => {
                params as *mut BCRYPT_PSS_PADDING_INFO as *mut std::ffi::c_void
            }
        }
    }

    fn flags(&self) -> u32 {
        match *self {
            SignParams::EC => 0,
            SignParams::RSA_PKCS1(_) => NCRYPT_PAD_PKCS1_FLAG,
            SignParams::RSA_PSS(_) => NCRYPT_PAD_PSS_FLAG,
        }
    }
}

/// Helper struct to hold onto OS-specific handles that must only be used on a particular thread.
struct ThreadSpecificHandles {
    /// The only thread that these handles may be used on.
    thread: RefPtr<nsIEventTarget>,
    /// A handle on the OS mechanism that represents the certificate for a key.
    cert: Option<CertContext>,
    /// A handle on the OS mechanism that represents a key.
    key: Option<KeyHandle>,
}

impl ThreadSpecificHandles {
    fn new(cert: CertContext, thread: &nsIEventTarget) -> ThreadSpecificHandles {
        ThreadSpecificHandles {
            thread: RefPtr::new(thread),
            cert: Some(cert),
            key: None,
        }
    }

    fn sign_or_get_signature_length(
        &mut self,
        key_type: KeyType,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
        do_signature: bool,
    ) -> Result<Vec<u8>, Error> {
        let Some(cert) = self.cert.take() else {
            return Err(error_here!(ErrorType::LibraryFailure));
        };
        let mut maybe_key = self.key.take();
        let thread = self.thread.clone();
        let data = data.to_vec();
        let params = params.clone();
        let task = moz_task::spawn_onto("sign", &thread, async move {
            let result = sign_internal(
                &cert,
                &mut maybe_key,
                key_type,
                &data,
                &params,
                do_signature,
            );
            if result.is_ok() {
                return (result, cert, maybe_key);
            }
            // Some devices appear to not work well when the key handle is held for too long or if a
            // card is inserted/removed while Firefox is running. Try refreshing the key handle.
            let _ = maybe_key.take();
            (
                sign_internal(
                    &cert,
                    &mut maybe_key,
                    key_type,
                    &data,
                    &params,
                    do_signature,
                ),
                cert,
                maybe_key,
            )
        });
        let (signature_result, cert, maybe_key) = futures_executor::block_on(task);
        self.cert = Some(cert);
        self.key = maybe_key;
        signature_result
    }
}

/// data: the data to sign
/// do_signature: if true, actually perform the signature. Otherwise, return a `Vec<u8>` of the
/// length the signature would be, if performed.
fn sign_internal(
    cert: &CertContext,
    maybe_key: &mut Option<KeyHandle>,
    key_type: KeyType,
    data: &[u8],
    params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    do_signature: bool,
) -> Result<Vec<u8>, Error> {
    // If this key hasn't been used for signing yet, there won't be a cached key handle. Obtain
    // and cache it if this is the case. Doing so can cause the underlying implementation to
    // show an authentication or pin prompt to the user. Caching the handle can avoid causing
    // multiple prompts to be displayed in some cases.
    if maybe_key.is_none() {
        let _ = maybe_key.replace(KeyHandle::from_cert(cert)?);
    }
    let Some(key) = maybe_key.as_ref() else {
        return Err(error_here!(ErrorType::LibraryFailure));
    };
    key.sign(data, params, do_signature, key_type)
}

impl Drop for ThreadSpecificHandles {
    fn drop(&mut self) {
        // Ensure any OS handles are dropped on the appropriate thread.
        let cert = self.cert.take();
        let key = self.key.take();
        let thread = self.thread.clone();
        // It is possible that we're already on the appropriate thread (e.g. if an error was
        // encountered in `find_objects` and these handles are being released shortly after being
        // created).
        if moz_task::is_on_current_thread(&thread) {
            drop(cert);
            drop(key);
        } else {
            let task = moz_task::spawn_onto("drop", &thread, async move {
                drop(cert);
                drop(key);
            });
            futures_executor::block_on(task)
        }
    }
}

/// Represents a private key for which there exists a corresponding certificate.
pub struct Key {
    /// The OS handles for this key. May only be used on the thread they were created on.
    handles: ThreadSpecificHandles,
    /// The decoded information about the key.
    cryptoki_key: CryptokiKey,
}

impl Key {
    fn new(cert_context: PCCERT_CONTEXT, thread: &nsIEventTarget) -> Result<Key, Error> {
        let cert = unsafe { *cert_context };
        let cert_der =
            unsafe { slice::from_raw_parts(cert.pbCertEncoded, cert.cbCertEncoded as usize) };
        let cert_info = unsafe { &*cert.pCertInfo };
        let spki = &cert_info.SubjectPublicKeyInfo;
        let algorithm_oid = unsafe { CStr::from_ptr(spki.Algorithm.pszObjId) }
            .to_str()
            .map_err(|_| error_here!(ErrorType::ExternalError))?;
        let (modulus, ec_params) = if algorithm_oid == szOID_RSA_RSA {
            if spki.PublicKey.cUnusedBits != 0 {
                return Err(error_here!(ErrorType::ExternalError));
            }
            let public_key_bytes = unsafe {
                std::slice::from_raw_parts(spki.PublicKey.pbData, spki.PublicKey.cbData as usize)
            };
            let modulus = read_rsa_modulus(public_key_bytes)?;
            (Some(modulus), None)
        } else if algorithm_oid == szOID_ECC_PUBLIC_KEY {
            let params = &spki.Algorithm.Parameters;
            let ec_params =
                unsafe { std::slice::from_raw_parts(params.pbData, params.cbData as usize) }
                    .to_vec();
            (None, Some(ec_params))
        } else {
            return Err(error_here!(ErrorType::LibraryFailure));
        };
        let cert = CertContext::new(cert_context);
        Ok(Key {
            handles: ThreadSpecificHandles::new(cert, thread),
            cryptoki_key: CryptokiKey::new(modulus, ec_params, cert_der)?,
        })
    }

    fn sign_or_get_signature_length(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
        do_signature: bool,
    ) -> Result<Vec<u8>, Error> {
        self.handles.sign_or_get_signature_length(
            self.cryptoki_key.key_type(),
            data,
            params,
            do_signature,
        )
    }
}

impl Sign for Key {
    fn get_signature_length(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<usize, Error> {
        self.sign_or_get_signature_length(data, params, false)
            .map(|signature| signature.len())
    }

    fn sign(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<Vec<u8>, Error> {
        self.sign_or_get_signature_length(data, params, true)
    }
}

impl CryptokiObject for Key {
    fn matches(&self, attrs: &[(CK_ATTRIBUTE_TYPE, Vec<u8>)]) -> bool {
        self.cryptoki_key.matches(attrs)
    }

    fn get_attribute(&self, attribute: CK_ATTRIBUTE_TYPE) -> Option<&[u8]> {
        self.cryptoki_key.get_attribute(attribute)
    }
}

struct CertStore {
    handle: HCERTSTORE,
}

impl Drop for CertStore {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe {
                CertCloseStore(self.handle, 0);
            }
        }
    }
}

impl Deref for CertStore {
    type Target = HCERTSTORE;

    fn deref(&self) -> &Self::Target {
        &self.handle
    }
}

impl CertStore {
    fn new(handle: HCERTSTORE) -> CertStore {
        CertStore { handle }
    }
}

// Given a pointer to a CERT_CHAIN_CONTEXT, enumerates each chain in the context and each element
// in each chain to gather every CERT_CONTEXT pointed to by the CERT_CHAIN_CONTEXT.
// https://docs.microsoft.com/en-us/windows/win32/api/wincrypt/ns-wincrypt-cert_chain_context says
// that the 0th element of the 0th chain will be the end-entity certificate. This certificate (if
// present), will be the 0th element of the returned Vec.
fn gather_cert_contexts(cert_chain_context: *const CERT_CHAIN_CONTEXT) -> Vec<*const CERT_CONTEXT> {
    let mut cert_contexts = Vec::new();
    if cert_chain_context.is_null() {
        return cert_contexts;
    }
    let cert_chain_context = unsafe { &*cert_chain_context };
    let cert_chains = unsafe {
        std::slice::from_raw_parts(
            cert_chain_context.rgpChain,
            cert_chain_context.cChain as usize,
        )
    };
    for cert_chain in cert_chains {
        // First dereference the borrow.
        let cert_chain = *cert_chain;
        if cert_chain.is_null() {
            continue;
        }
        // Then dereference the pointer.
        let cert_chain = unsafe { &*cert_chain };
        let chain_elements = unsafe {
            std::slice::from_raw_parts(cert_chain.rgpElement, cert_chain.cElement as usize)
        };
        for chain_element in chain_elements {
            let chain_element = *chain_element; // dereference borrow
            if chain_element.is_null() {
                continue;
            }
            let chain_element = unsafe { &*chain_element }; // dereference pointer
            cert_contexts.push(chain_element.pCertContext);
        }
    }
    cert_contexts
}

pub struct Backend {
    /// A background thread that all OS API calls will be done on. This is to prevent issues with
    /// modules or implementations using thread-local state.
    thread: RefPtr<nsIEventTarget>,
    /// The last time a call to `find_objects` finished, to avoid searching for objects more than
    /// once every 3 seconds.
    last_scan_finished: Option<Instant>,
}

impl Backend {
    pub fn new() -> Result<Backend, Error> {
        let thread = moz_task::create_thread("osclientcerts").map_err(|nsresult| {
            error_here!(ErrorType::LibraryFailure, nsresult.error_name().to_string())
        })?;
        Ok(Backend {
            thread: thread
                .query_interface::<nsIEventTarget>()
                .ok_or(error_here!(ErrorType::LibraryFailure))?,
            last_scan_finished: None,
        })
    }
}

const SLOT_DESCRIPTION_BYTES: &[u8; 64] =
    b"OS Client Cert Slot                                             ";
const TOKEN_LABEL_BYTES: &[u8; 32] = b"OS Client Cert Token            ";
const TOKEN_MODEL_BYTES: &[u8; 16] = b"osclientcerts   ";
const TOKEN_SERIAL_NUMBER_BYTES: &[u8; 16] = b"0000000000000000";

impl ClientCertsBackend for Backend {
    type Key = Key;

    fn find_objects(&mut self) -> Result<(Vec<CryptokiCert>, Vec<Key>), Error> {
        match self.last_scan_finished {
            Some(last_scan_finished) => {
                if Instant::now().duration_since(last_scan_finished) < Duration::new(3, 0) {
                    return Ok((Vec::new(), Vec::new()));
                }
            }
            None => {}
        }

        let thread = self.thread.clone();
        let task = moz_task::spawn_onto("find_objects", &self.thread, async move {
            find_objects(&thread)
        });
        let result = futures_executor::block_on(task);
        self.last_scan_finished = Some(Instant::now());
        result
    }

    fn get_slot_info(&self) -> CK_SLOT_INFO {
        CK_SLOT_INFO {
            slotDescription: *SLOT_DESCRIPTION_BYTES,
            manufacturerID: *crate::MANUFACTURER_ID_BYTES,
            flags: CKF_TOKEN_PRESENT,
            ..Default::default()
        }
    }

    fn get_token_info(&self) -> CK_TOKEN_INFO {
        CK_TOKEN_INFO {
            label: *TOKEN_LABEL_BYTES,
            manufacturerID: *crate::MANUFACTURER_ID_BYTES,
            model: *TOKEN_MODEL_BYTES,
            serialNumber: *TOKEN_SERIAL_NUMBER_BYTES,
            ..Default::default()
        }
    }

    fn get_mechanism_list(&self) -> Vec<CK_MECHANISM_TYPE> {
        vec![CKM_ECDSA, CKM_RSA_PKCS, CKM_RSA_PKCS_PSS]
    }
}

/// Attempts to enumerate certificates with private keys exposed by the OS. Currently only looks in
/// the "My" cert store of the current user. In the future this may look in more locations.
fn find_objects(thread: &nsIEventTarget) -> Result<(Vec<CryptokiCert>, Vec<Key>), Error> {
    let mut certs = Vec::new();
    let mut keys = Vec::new();
    let location_flags =
        CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG;
    let store_name = match CString::new("My") {
        Ok(store_name) => store_name,
        Err(_) => return Err(error_here!(ErrorType::LibraryFailure)),
    };
    let store = CertStore::new(unsafe {
        CertOpenStore(
            CERT_STORE_PROV_SYSTEM_REGISTRY_A,
            0,
            0,
            location_flags,
            store_name.as_ptr() as *const winapi::ctypes::c_void,
        )
    });
    if store.is_null() {
        return Err(error_here!(ErrorType::ExternalError));
    }
    let find_params = CERT_CHAIN_FIND_ISSUER_PARA {
        cbSize: std::mem::size_of::<CERT_CHAIN_FIND_ISSUER_PARA>() as u32,
        pszUsageIdentifier: std::ptr::null(),
        dwKeySpec: 0,
        dwAcquirePrivateKeyFlags: 0,
        cIssuer: 0,
        rgIssuer: std::ptr::null_mut(),
        pfnFindCallback: None,
        pvFindArg: std::ptr::null_mut(),
        pdwIssuerChainIndex: std::ptr::null_mut(),
        pdwIssuerElementIndex: std::ptr::null_mut(),
    };
    let mut cert_chain_context: PCCERT_CHAIN_CONTEXT = std::ptr::null_mut();
    loop {
        // CertFindChainInStore finds all certificates with private keys in the store. It also
        // attempts to build a verified certificate chain to a trust anchor for each certificate.
        // We gather and hold onto these extra certificates so that gecko can use them when
        // filtering potential client certificates according to the acceptable CAs list sent by
        // servers when they request client certificates.
        cert_chain_context = unsafe {
            CertFindChainInStore(
                *store,
                X509_ASN_ENCODING,
                CERT_CHAIN_FIND_BY_ISSUER_CACHE_ONLY_FLAG
                    | CERT_CHAIN_FIND_BY_ISSUER_CACHE_ONLY_URL_FLAG,
                CERT_CHAIN_FIND_BY_ISSUER,
                &find_params as *const CERT_CHAIN_FIND_ISSUER_PARA as *const winapi::ctypes::c_void,
                cert_chain_context,
            )
        };
        if cert_chain_context.is_null() {
            break;
        }
        let cert_contexts = gather_cert_contexts(cert_chain_context);
        // The 0th CERT_CONTEXT is the end-entity (i.e. the certificate with the private key we're
        // after).
        match cert_contexts.get(0) {
            Some(cert_context) => {
                let key = match Key::new(*cert_context, thread) {
                    Ok(key) => key,
                    Err(_) => continue,
                };
                let cert = match new_cert(*cert_context) {
                    Ok(cert) => cert,
                    Err(_) => continue,
                };
                certs.push(cert);
                keys.push(key);
            }
            None => {}
        };
        for cert_context in cert_contexts.iter().skip(1) {
            if let Ok(cert) = new_cert(*cert_context) {
                certs.push(cert);
            }
        }
    }
    Ok((certs, keys))
}
