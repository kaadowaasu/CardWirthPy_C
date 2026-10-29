package jp.cardwirth.sdlsample;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.widget.TextView;

/**
 * Android の UI スレッドを止めず、不変の同梱素材を導
 * 入する。トランザクションによる導入が確定してから SDL を起
 * 動し、ネイティブ実行環境に準備中・巻き戻し中の世代を見せない。
 */
public final class BootstrapActivity extends Activity {
    private TextView status;
    private volatile boolean destroyed;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setText("CardWirthPy_C を準備しています…");
        setContentView(status);

        final Intent source = getIntent();
        new Thread(() -> {
            try {
                MainActivity.installBundledData(getApplicationContext());
                runOnUiThread(() -> launchMain(source));
            } catch (Exception error) {
                Log.e("CardWirthPy_C", "Bundled Data extraction failed", error);
                runOnUiThread(() -> showFailure(error));
            }
        }, "CardWirthPy_C asset installer").start();
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        super.onDestroy();
    }

    private void launchMain(Intent source) {
        if (destroyed || isFinishing()) return;
        final Intent target = new Intent(this, MainActivity.class);
        if (source != null) {
            if (source.getExtras() != null) target.putExtras(source.getExtras());
            target.setData(source.getData());
        }
        startActivity(target);
        finish();
    }

    private void showFailure(Exception error) {
        if (destroyed || isFinishing()) return;
        status.setText("起動データを準備できませんでした。\n" +
            (error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage()));
    }
}
