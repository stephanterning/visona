# macOS signing and notarization

The Release workflow signs the macOS app and the VST3, AU and CLAP plugins with a Developer ID, and has Apple notarize them, so they open without Gatekeeper warnings (D-111). It needs an Apple Developer Program membership and five repository secrets. Local builds stay ad-hoc signed.

## Secrets

Set them under **Settings → Secrets and variables → Actions** on GitHub, or with `gh secret set`.

| Secret | Value |
| --- | --- |
| `MACOS_CERTIFICATE_P12` | The Developer ID Application certificate and its private key, as a base64 `.p12` |
| `MACOS_CERTIFICATE_PASSWORD` | The password the `.p12` was exported with |
| `APPLE_ID` | The Apple ID email of the developer account |
| `APPLE_APP_PASSWORD` | An app-specific password for that Apple ID |
| `APPLE_TEAM_ID` | The 10-character Team ID |

Without `MACOS_CERTIFICATE_P12`, a manual run signs ad hoc and warns, and a published release fails.

## Create the certificate

1. In **Keychain Access**, choose **Keychain Access → Certificate Assistant → Request a Certificate From a Certificate Authority**. Enter the Apple ID email, choose **Saved to disk**, and save the `.certSigningRequest` file.
2. On [developer.apple.com/account/resources/certificates](https://developer.apple.com/account/resources/certificates/list), press **+**, choose **Developer ID Application**, the **G2 Sub-CA** profile, and upload the request. Download the `.cer` and double-click it, so it lands in the login keychain next to its private key.
3. In Keychain Access, under **My Certificates**, right-click **Developer ID Application: *name* (*team ID*)**, choose **Export**, save it as `DeveloperID.p12` and give it a strong password.
4. Store it, then delete the file:

   ```sh
   base64 -i DeveloperID.p12 | gh secret set MACOS_CERTIFICATE_P12
   gh secret set MACOS_CERTIFICATE_PASSWORD
   rm DeveloperID.p12
   ```

Keep the certificate in the keychain, or the `.p12` in a password manager: a Developer ID certificate cannot be downloaded again with its private key.

## Notarization credentials

1. On [account.apple.com](https://account.apple.com), under **Sign-In and Security → App-Specific Passwords**, create one named `Visona notarization`.
2. Find the Team ID under **Membership details** on [developer.apple.com/account](https://developer.apple.com/account).
3. Store them:

   ```sh
   gh secret set APPLE_ID
   gh secret set APPLE_APP_PASSWORD
   gh secret set APPLE_TEAM_ID
   ```

## Check

Run the Release workflow by hand with the macOS boxes ticked. The **Import signing certificate** step prints the identity, **Notarize** prints the result from the notary service, and a rejection prints its log. On a downloaded build:

```sh
spctl --assess --type execute --verbose=2 Visona.app
codesign -dv --verbose=2 ~/Library/Audio/Plug-Ins/VST3/Visona.vst3
xcrun stapler validate ~/Library/Audio/Plug-Ins/Components/Visona.component
```

`spctl` should say `accepted` and `source=Notarized Developer ID`.

## Renewal

A Developer ID certificate is valid for five years. Signed and notarized builds keep working after it expires, because they carry a secure timestamp. Before it expires, create a new one as above and replace the two certificate secrets. If the membership lapses, notarization stops, but builds published before keep working. A new app-specific password is needed if the Apple ID password changes.
