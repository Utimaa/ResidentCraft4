package dev.re4craft.client;

import dev.re4craft.protocol.Wire;
import net.minecraft.client.Minecraft;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonInfo;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.gui.screens.ChatScreen;
import dev.re4craft.client.mixin.KeyMappingAccessor;
import org.lwjgl.sdl.SDLKeyboard;
import com.mojang.blaze3d.platform.InputConstants;

/** Feeds actions through vanilla handlers instead of reimplementing Minecraft mechanics. */
public final class VirtualInput {
    private static final int[] SCANS = {26,22,4,7,44,225,224}; // W,S,A,D,Space,LShift,LCtrl
    private static final boolean[] KEYS = new boolean[512];
    private static final boolean[] MOUSE = new boolean[4];
    private static int modifiers;
    private static double cursorX,cursorY;
    private static boolean screenWasOpen;
    private static final boolean[] ACTIONS=new boolean[7];
    private static KeyMapping boundInventory;
    private VirtualInput() { }
    public static boolean isKeyDown(int code) { return code >= 0 && code < KEYS.length && KEYS[code]; }
    public static void apply(Minecraft mc, Wire.Input input, boolean fresh) {
        if (!input.active() || mc.player == null) { releaseAll(mc); return; }
        // Bind the vanilla screen as well as the host action: X must close the
        // inventory too, while remaining ordinary text in the chat screen.
        if(boundInventory!=mc.options.keyInventory) {
            boundInventory=mc.options.keyInventory;
            boundInventory.setKey(InputConstants.Type.KEYBOARD.getOrCreate(InputConstants.KEY_X));
            KeyMapping.resetMapping();
        }
        boolean screen = mc.gui.screen() != null;
        if(screenWasOpen && !screen)releaseAll(mc);
        screenWasOpen=screen;
        if(!screen) { mc.player.setYRot(input.yaw()); mc.player.setXRot(input.pitch()); }
        KeyMapping[] mappings={mc.options.keyUp,mc.options.keyDown,mc.options.keyLeft,mc.options.keyRight,
                mc.options.keyJump,mc.options.keyShift,mc.options.keySprint};
        for(int i=0;i<mappings.length;i++) {
            boolean down=!screen && ((input.flags()&16)==0 || Re4CraftClient.guestReady()) && (input.held()&(1<<i))!=0;
            // These are semantic Minecraft actions. AZERTY physical key Z means forward;
            // SDL's layout-dependent W->Z keycode must not change the vanilla action.
            if(ACTIONS[i]!=down) {ACTIONS[i]=down;mappings[i].setDown(down);}
            // GUI key events own their raw SDL states (Shift-click and Ctrl editing).
            // Releasing gameplay actions must not erase a held GUI modifier.
            if(!screen)KEYS[SCANS[i]]=down;
        }
        if(screen) {
            double x=input.cursorX()*mc.getWindow().getWidth(),y=input.cursorY()*mc.getWindow().getHeight();
            mc.mouseHandler.onMove(mc.getWindow().handle(),x,y,x-cursorX,y-cursorY);
            cursorX=x;cursorY=y;
        }
        setMouse(mc, 1, screen ? (input.mouseButtons() & 1)!=0 : (input.held() & 128) != 0);
        setMouse(mc, 3, screen ? (input.mouseButtons() & 2)!=0 : (input.held() & 256) != 0);
        setMouse(mc, 2, screen && (input.mouseButtons() & 4)!=0);
        if (!fresh) return;
        if (!screen && input.selectedSlot() != -1) mc.player.getInventory().setSelectedSlot(input.selectedSlot());
        if (input.scroll() != 0) mc.mouseHandler.onScroll(mc.getWindow().handle(), 0, input.scroll()/120.0);
        KeyMapping[] edges={mc.options.keyInventory,mc.options.keyDrop,mc.options.keySwapOffhand,mc.options.keyTogglePerspective};
        for(int i=0;i<edges.length;i++)if((input.pressed()&(1<<i))!=0) {
            KeyMappingAccessor key=(KeyMappingAccessor)edges[i];key.re4craft$clickCount(key.re4craft$clickCount()+1);
        }
        if((input.pressed()&16)!=0)mc.keyboardHandler.keyPress(mc.getWindow().handle(),1,new KeyEvent(41,27,0));
        if(!screen && (input.pressed()&32)!=0)mc.gui.setScreen(new ChatScreen("",false));
        if(!screen && (input.pressed()&64)!=0)mc.gui.setScreen(new ChatScreen("/",false));
    }
    public static void key(Minecraft mc,Wire.Key key) {
        if(!Re4CraftClient.active())return;
        KEYS[key.scancode()]=key.action()!=0;modifiers=key.modifiers();
        int code=SDLKeyboard.SDL_GetKeyFromScancode(key.scancode(),(short)modifiers,true);
        mc.keyboardHandler.keyPress(mc.getWindow().handle(),key.action(),new KeyEvent(key.scancode(),code,modifiers));
    }
    public static void text(Minecraft mc,String text) {
        if(Re4CraftClient.active() && mc.gui.screen()!=null)mc.keyboardHandler.textInput(mc.getWindow().handle(),text);
    }
    private static void setKey(Minecraft mc, int scan, boolean down) {
        if (KEYS[scan] == down) return;
        KEYS[scan] = down;
        modifiers = (KEYS[225] ? 1 : 0) | (KEYS[224] ? 0x40 : 0);
        int code = SDLKeyboard.SDL_GetKeyFromScancode(scan, (short)modifiers, true);
        mc.keyboardHandler.keyPress(mc.getWindow().handle(), down ? 1 : 0, new KeyEvent(scan,code,modifiers));
    }
    private static void setMouse(Minecraft mc, int button, boolean down) {
        if (MOUSE[button] == down) return;
        MOUSE[button] = down;
        mc.mouseHandler.onButton(mc.getWindow().handle(), new MouseButtonInfo(button,modifiers), down ? 1 : 0);
    }
    public static void releaseAll(Minecraft mc) {
        for (int i = 0; i < KEYS.length; i++) if (KEYS[i]) setKey(mc,i,false);
        for (int i = 1; i < MOUSE.length; i++) if (MOUSE[i]) setMouse(mc,i,false);
        KeyMapping[] mappings={mc.options.keyUp,mc.options.keyDown,mc.options.keyLeft,mc.options.keyRight,
                mc.options.keyJump,mc.options.keyShift,mc.options.keySprint};
        for(int i=0;i<mappings.length;i++) {ACTIONS[i]=false;mappings[i].setDown(false);}
    }
}
