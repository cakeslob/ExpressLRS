function arcadeTankMix(t, s) {
    // 1. Get X and Y from the Joystick, do whatever scaling and calibrating you need to do based on your hardware.
    // 2. Invert X
    // 3. Calculate R+L (Call it V): V = (100-ABS(X)) * (Y/100) + Y
    // 4. Calculate R-L (Call it W): W = (100-ABS(Y)) * (X/100) + X
    // 5. Calculate R: R = (V+W) / 2
    // 6. Calculate L: L = (V-W) / 2
    // 7. Do any scaling on R and L your hardware may require.
    // 8. Send those values to your Robot.
    // 9. Go back to 1.

    let invs = -s;
    let v = ((1 - Math.abs(s)) * t) + t;
    let w = ((1 - Math.abs(t)) * invs) + invs;
    let r = (v + w) / 2;
    let l = (v - w) / 2;
    return [clamp(l, -1, 1), clamp(r, -1, 1)];
}

function simpleTankMix(t, s, {fn = 0, thr_scale = 1, str_scale = 1, thr_dz = 0.05, str_dz = 0.05, thr_exp = 0, str_exp = 0, thr_trim = 0, str_trim = 0, left_scale = 1, right_scale = 1, left_dz = 0, right_dz = 0, left_exp = 0, right_exp = 0, left_trim = 0, right_trim = 0, left_chan = 0, right_chan = 1}) {
    let ret = true;
    if (typeof fn === 'function') {
        let ts = fn();
        t = ts[0];
        s = ts[1];
    }
    t += thr_trim;
    s += str_trim;
    t = applyDeadzone(t, thr_dz);
    s = applyDeadzone(s, str_dz);
    t = applyExpo(t, thr_exp);
    s = applyExpo(s, str_exp);
    t *= thr_scale;
    s *= str_scale;
    let lr = arcadeTankMix(t, s); 
    lr[0] = applyExpo(lr[0], left_exp)
    lr[1] = applyExpo(lr[1], right_exp)
    lr[0] = applyDeadzone(lr[0], left_dz)
    lr[1] = applyDeadzone(lr[1], right_dz)
    if (left_chan >= 0) {
        channel[left_chan] = scaleToCRSF(lr[0] + left_trim, left_scale);
    }
    if (right_chan >= 0) {
        channel[right_chan] = scaleToCRSF(lr[1] + right_trim, right_scale);
    }
    return ret;
}

function applyDeadzone(x, dz)
{
    if (dz >= 0)
    {
        // traditional deadzone implementation
        if (Math.abs(x) < dz) {
            return 0;
        }
        else if (x >= 0) {
            return mapRange(x, dz, 1, 0, 1, true);
        }
        else {
            return mapRange(x, -dz, -1, 0, -1, true);
        }
    }
    else
    {
        // anti-deadzone if negative, used for a start-up-boost type situation
        dz = -dz;
        if (x > 0 && x != 0) {
            return mapRange(x, 0, 1, dz, 1, true);
        }
        else if (x < 0 && x != 0) {
            return mapRange(x, -1, 0, -1, -dz, true);
        }
        else {
            return 0;
        }
    }
}

function applyExpo(x, ex) {
    // applying an expo curve can make a robot easier to drive
    var absx = Math.abs(x);
    var absy = absx * Math.exp(ex * (absx - 1));
    return absy * ((x >= 0) ? 1 : -1);
}
