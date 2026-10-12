// Forsaken VR on Quest and Steam Frame (standalone): a helper for the PC's
// Setup. A folder adb makes belongs to adb, and the game could not save its
// settings, pilots or games into it. So Setup asks the app, with an explicit
// broadcast, to make the writable folders itself and take in the files Setup
// copied as "name.restore". It answers with how many folders it can write.
// No window, nothing else.
package net.gameordie.forsakenvr;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class SetupReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        int w = ForsakenActivity.prepare(context);
        Log.i(ForsakenActivity.TAG, "Setup: " + w + " of " + ForsakenActivity.WRITABLE.length + " folders writable");
        setResultCode(w);
        setResultData(Integer.toString(w));
    }
}
