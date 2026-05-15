<script lang="ts">
	import { onDestroy, tick } from 'svelte';
	import * as Dialog from '$lib/components/ui/dialog';
	import { Button } from '$lib/components/ui/button';
	import { Input } from '$lib/components/ui/input';
	import { Camera, ImagePlus, Mic, RefreshCw, ShieldCheck } from '@lucide/svelte';

	type EnrollmentModality = 'face' | 'voice';
	type FaceAngle = 'front' | 'left' | 'right';
	type VoiceSample = {
		base64: string;
		mimeType: string;
		transcript: string;
		warnings: string[];
	};
	type PreviousFaceCapture = {
		identityId: string;
		name: string;
		namespace: string;
		front: string;
		left: string;
		right: string;
	};

	const faceAngles: { key: FaceAngle; label: string; cue: string }[] = [
		{ key: 'front', label: 'Forward', cue: 'Look forward' },
		{ key: 'left', label: 'Left', cue: 'Look left' },
		{ key: 'right', label: 'Right', cue: 'Look right' }
	];
	const voicePhrase = 'Guardian, remember my voice for local identity recognition.';

	interface Props {
		open?: boolean;
		modality?: EnrollmentModality;
		namespace?: string;
		defaultName?: string;
		onOpenChange?: (open: boolean) => void;
	}

	let {
		open = $bindable(false),
		modality = 'face',
		namespace = 'profile:Iain/personal',
		defaultName = '',
		onOpenChange
	}: Props = $props();

	let name = $state('');
	let stream: MediaStream | null = $state(null);
	let videoRef: HTMLVideoElement | undefined = $state();
	let canvasRef: HTMLCanvasElement | undefined = $state();
	let profilePhotoInput: HTMLInputElement | undefined = $state();
	let mediaRecorder: MediaRecorder | null = $state(null);
	let recordingChunks: Blob[] = [];
	let isStarting = $state(false);
	let isRecording = $state(false);
	let faceStreamHasAudio = $state(false);
	let isSubmitting = $state(false);
	let permissionPrompted = $state(false);
	let status = $state('');
	let error = $state('');
	let permissionHint = $state('');
	let capturedFaces = $state<Record<FaceAngle, string>>({ front: '', left: '', right: '' });
	let activeFaceAngle = $state<FaceAngle>('front');
	let capturedVoiceBase64 = $state('');
	let capturedVoiceMimeType = $state('');
	let heardVoiceText = $state('');
	let faceQuality = $state('');
	let faceQualityState = $state<'searching' | 'aligning' | 'ready'>('searching');
	let facePosePromptUntil = 0;
	let faceStableFrames = 0;
	let faceAutoTimer: number | undefined;
	let faceAutoBusy = false;
	let startedFor = $state('');
	let previousFaceCaptures = $state<PreviousFaceCapture[]>([]);
	let previousFaceLoadSeq = 0;
	let previousFaceLoadTimer: number | undefined;

	const title = $derived(modality === 'face' ? 'Remember My Face' : 'Remember My Voice');
	const prompt = $derived(
		modality === 'face'
			? 'Use the placement guide to capture front, left, and right face samples for this profile.'
			: 'Record a short phrase so Gateway can bind a bounded voice print to this profile.'
	);
	let capturedFaceCount = $derived(faceAngles.filter((angle) => capturedFaces[angle.key]).length);
	let activeFace = $derived(faceAngles.find((angle) => angle.key === activeFaceAngle) ?? faceAngles[0]);
	let guideClass = $derived(
		faceQualityState === 'ready'
			? 'border-emerald-300 shadow-[0_0_0_999px_rgba(0,0,0,0.22),0_0_42px_rgba(52,211,153,0.75)]'
			: faceQualityState === 'aligning'
				? 'border-amber-300 shadow-[0_0_0_999px_rgba(0,0,0,0.30),0_0_34px_rgba(252,211,77,0.60)]'
				: 'border-red-300 shadow-[0_0_0_999px_rgba(0,0,0,0.36),0_0_34px_rgba(252,165,165,0.62)]'
	);

	$effect(() => {
		if (defaultName && !name) name = defaultName;
	});

	$effect(() => {
		if (open && startedFor !== modality) {
			startedFor = modality;
			resetEnrollmentState();
			void maybeAutoStartCapture();
		}
		if (!open) {
			startedFor = '';
			stopCapture();
		}
	});

	$effect(() => {
		const currentName = name.trim();
		const currentNamespace = namespace;
		if (!open || modality !== 'face') {
			previousFaceCaptures = [];
			return;
		}
		if (previousFaceLoadTimer !== undefined) window.clearTimeout(previousFaceLoadTimer);
		previousFaceLoadTimer = window.setTimeout(() => {
			void loadPreviousFaceCaptures(currentNamespace, currentName);
		}, 250);
	});

	function handleOpenChange(nextOpen: boolean) {
		open = nextOpen;
		onOpenChange?.(nextOpen);
	}

	function stopCapture() {
		if (mediaRecorder?.state === 'recording') mediaRecorder.stop();
		mediaRecorder = null;
		isRecording = false;
		if (videoRef) {
			videoRef.pause();
			videoRef.srcObject = null;
		}
		if (faceAutoTimer !== undefined) {
			window.clearInterval(faceAutoTimer);
			faceAutoTimer = undefined;
		}
		if (previousFaceLoadTimer !== undefined) {
			window.clearTimeout(previousFaceLoadTimer);
			previousFaceLoadTimer = undefined;
		}
		faceAutoBusy = false;
		faceStableFrames = 0;
		facePosePromptUntil = 0;
		faceQuality = '';
		faceQualityState = 'searching';
		stream?.getTracks().forEach((track) => track.stop());
		stream = null;
	}

	function resetEnrollmentState() {
		stopCapture();
		error = '';
		permissionHint = permissionHelpText();
		status =
			modality === 'face'
				? 'Click Allow camera and microphone to start enrollment.'
				: 'Click Allow microphone to start enrollment.';
		permissionPrompted = false;
		capturedFaces = { front: '', left: '', right: '' };
		activeFaceAngle = 'front';
		capturedVoiceBase64 = '';
		capturedVoiceMimeType = '';
		heardVoiceText = '';
		faceQuality = '';
		faceQualityState = 'searching';
		facePosePromptUntil = 0;
		faceStreamHasAudio = false;
	}

	async function loadPreviousFaceCaptures(currentNamespace: string, currentName: string) {
		const seq = ++previousFaceLoadSeq;
		try {
			const response = await fetch('./guardian/v1/perception/status');
			if (!response.ok) return;
			const payload = await response.json();
			if (seq !== previousFaceLoadSeq) return;
			const identities = Array.isArray(payload?.identities) ? payload.identities : [];
			const normalizedName = currentName.trim().toLowerCase();
			previousFaceCaptures = identities
				.filter((identity) => {
					if (!identity || identity.disabled) return false;
					if (identity.namespace !== currentNamespace) return false;
					if (normalizedName && String(identity.name || '').trim().toLowerCase() !== normalizedName) return false;
					return Boolean(
						identity.face_thumbnail_base64 ||
							identity.face_left_thumbnail_base64 ||
							identity.face_right_thumbnail_base64
					);
				})
				.slice(-3)
				.reverse()
				.map((identity) => ({
					identityId: String(identity.identity_id || ''),
					name: String(identity.name || 'Previous'),
					namespace: String(identity.namespace || ''),
					front: String(identity.face_thumbnail_base64 || ''),
					left: String(identity.face_left_thumbnail_base64 || ''),
					right: String(identity.face_right_thumbnail_base64 || '')
				}));
		} catch {
			if (seq === previousFaceLoadSeq) previousFaceCaptures = [];
		}
	}

	function mediaGrantStorageKey(): string {
		return `aigateway.identity.mediaGranted.${modality}`;
	}

	function rememberMediaGrant() {
		try {
			window.localStorage.setItem(mediaGrantStorageKey(), 'true');
		} catch {
			// Non-critical convenience flag only.
		}
	}

	function hasRememberedMediaGrant(): boolean {
		try {
			return window.localStorage.getItem(mediaGrantStorageKey()) === 'true';
		} catch {
			return false;
		}
	}

	async function maybeAutoStartCapture() {
		await tick();
		if (!open || stream || isStarting || !isTrustedMediaOrigin()) return;
		let canAutoStart = hasRememberedMediaGrant();
		try {
			const permissionsApi = navigator.permissions as Permissions | undefined;
			const cameraPermission = modality === 'face'
				? await permissionsApi?.query({ name: 'camera' as PermissionName })
				: undefined;
			const microphonePermission = await permissionsApi?.query({ name: 'microphone' as PermissionName });
			canAutoStart =
				canAutoStart ||
				(modality === 'face'
					? cameraPermission?.state === 'granted' && microphonePermission?.state === 'granted'
					: microphonePermission?.state === 'granted');
		} catch {
			// Some browsers hide camera/microphone permission state.
		}
		if (canAutoStart) void startCapture(true);
	}

	function isTrustedMediaOrigin(): boolean {
		if (typeof window === 'undefined') return false;
		const localHost = ['localhost', '127.0.0.1', '::1'].includes(window.location.hostname);
		return window.isSecureContext || localHost;
	}

	function permissionHelpText(): string {
		if (typeof window === 'undefined') return '';
		if (isTrustedMediaOrigin()) {
			return 'Your browser should show a permission prompt after you press Allow.';
		}
		return 'Camera and microphone prompts require HTTPS or a trusted local origin. This HTTP page cannot trigger the browser prompt.';
	}

	function httpsHref(): string {
		if (typeof window === 'undefined') return '';
		return `https://${window.location.host}${window.location.pathname}${window.location.search}${window.location.hash}`;
	}

	function mediaPermissionErrorMessage(err: unknown): string {
		const name = err instanceof DOMException || err instanceof Error ? err.name : '';
		if (name === 'NotAllowedError' || name === 'PermissionDeniedError') {
			return modality === 'face'
				? 'Camera or microphone permission was blocked. Use the browser permission icon near the address bar to allow camera and microphone for this site, then press Reset.'
				: 'Microphone permission was blocked. Use the browser permission icon near the address bar to allow microphone for this site, then press Reset.';
		}
		if (name === 'NotFoundError' || name === 'DevicesNotFoundError') {
			return modality === 'face'
				? 'No camera was found. Check the camera is connected and not already in use.'
				: 'No microphone was found. Check the microphone is connected and not already in use.';
		}
		if (name === 'NotReadableError' || name === 'TrackStartError') {
			return 'The browser can see the device, but cannot open it. Another app may already be using it.';
		}
		if (name === 'SecurityError') {
			return 'The browser blocked device access for this page. Camera and microphone access requires HTTPS or a trusted local origin.';
		}
		return err instanceof Error ? err.message : 'Unable to open device.';
	}

	async function startCapture(autoStart = false) {
		permissionHint = permissionHelpText();
		if (!isTrustedMediaOrigin()) {
			error = 'Browser permission prompt blocked: camera and microphone access requires HTTPS or a trusted local origin.';
			return;
		}
		if (typeof navigator === 'undefined' || !navigator.mediaDevices?.getUserMedia) {
			error = 'This browser cannot access camera or microphone devices.';
			return;
		}
		if (modality === 'voice' && typeof MediaRecorder === 'undefined') {
			error = 'This browser cannot record microphone samples.';
			return;
		}
		stopCapture();
		error = '';
		status = '';
		permissionPrompted = true;
		isStarting = true;
		capturedFaces = { front: '', left: '', right: '' };
		activeFaceAngle = 'front';
		capturedVoiceBase64 = '';
		faceStreamHasAudio = false;
		try {
			if (modality === 'face') {
				try {
					stream = await navigator.mediaDevices.getUserMedia({
						video: { width: { ideal: 640 }, height: { ideal: 480 }, facingMode: 'user' },
						audio: { echoCancellation: true, noiseSuppression: true }
					});
					faceStreamHasAudio = stream.getAudioTracks().length > 0;
				} catch (err) {
					const name = err instanceof DOMException || err instanceof Error ? err.name : '';
					if (name === 'NotAllowedError' || name === 'PermissionDeniedError') throw err;
					stream = await navigator.mediaDevices.getUserMedia({
						video: { width: { ideal: 640 }, height: { ideal: 480 }, facingMode: 'user' },
						audio: false
					});
				}
			} else {
				stream = await navigator.mediaDevices.getUserMedia({
					audio: { echoCancellation: true, noiseSuppression: true },
					video: false
				});
			}
			await tick();
			if (videoRef && modality === 'face') {
				videoRef.srcObject = stream;
				videoRef.muted = true;
				await videoRef.play();
				startFaceAutoCapture();
			}
			status =
				modality === 'face'
					? faceStreamHasAudio
						? 'Camera and microphone ready. Hold each pose inside the guide and I will capture automatically.'
						: 'Camera ready'
					: 'Microphone ready';
			rememberMediaGrant();
		} catch (err) {
			if (!autoStart) error = mediaPermissionErrorMessage(err);
		} finally {
			isStarting = false;
		}
	}

	function decodedBase64Size(value: string): number {
		const encoded = value.includes(',') ? value.split(',', 2)[1] : value;
		const padding = encoded.endsWith('==') ? 2 : encoded.endsWith('=') ? 1 : 0;
		return Math.max(0, Math.floor((encoded.length * 3) / 4) - padding);
	}

	async function captureFace() {
		if (!videoRef || !canvasRef) return;
		const width = videoRef.videoWidth || 640;
		const height = videoRef.videoHeight || 480;
		const guideWidth = width * 0.58;
		const guideHeight = height * 0.68;
		const sourceWidth = Math.min(width, guideWidth * 0.9);
		const sourceHeight = Math.min(height, guideHeight * 0.92);
		const sourceX = Math.max(0, (width - sourceWidth) / 2);
		const sourceY = Math.max(0, (height - sourceHeight) / 2);
		canvasRef.width = 220;
		canvasRef.height = 260;
		const ctx = canvasRef.getContext('2d');
		if (!ctx) return;
		ctx.fillStyle = '#030712';
		ctx.fillRect(0, 0, canvasRef.width, canvasRef.height);
		ctx.save();
		ctx.beginPath();
		ctx.ellipse(
			canvasRef.width / 2,
			canvasRef.height / 2,
			canvasRef.width * 0.5,
			canvasRef.height * 0.5,
			0,
			0,
			Math.PI * 2
		);
		ctx.clip();
		ctx.drawImage(videoRef, sourceX, sourceY, sourceWidth, sourceHeight, 0, 0, canvasRef.width, canvasRef.height);
		ctx.restore();
		let quality = 0.82;
		let dataUrl = canvasRef.toDataURL('image/jpeg', quality);
		while (decodedBase64Size(dataUrl) > 32 * 1024 && quality > 0.42) {
			quality -= 0.1;
			dataUrl = canvasRef.toDataURL('image/jpeg', quality);
		}
		capturedFaces = { ...capturedFaces, [activeFaceAngle]: dataUrl };
		playCaptureClick();
		const nextMissing = faceAngles.find((angle) => !capturedFaces[angle.key] && angle.key !== activeFaceAngle);
		if (nextMissing) activeFaceAngle = nextMissing.key;
		faceStableFrames = 0;
		facePosePromptUntil = Date.now() + 2400;
		status = nextMissing
			? `${activeFace.label} face sample captured. Now ${nextMissing.cue.toLowerCase()}.`
			: 'Front, left, and right face samples captured. Record the voice phrase when ready.';
	}

	async function handleProfilePhotoSelected(event: Event) {
		const input = event.currentTarget as HTMLInputElement;
		const file = input.files?.[0];
		input.value = '';
		if (!file) return;
		if (!file.type.startsWith('image/')) {
			error = 'Choose an image file for the profile photo.';
			return;
		}
		if (file.size > 6 * 1024 * 1024) {
			error = 'Profile photo must be 6 MiB or smaller.';
			return;
		}
		try {
			const dataUrl = await imageFileToFaceDataUrl(file);
			capturedFaces = { ...capturedFaces, front: dataUrl };
			if (activeFaceAngle === 'front') activeFaceAngle = capturedFaces.left ? 'right' : 'left';
			faceStableFrames = 0;
			facePosePromptUntil = Date.now() + 1600;
			error = '';
			status = 'Profile photo loaded as the front face sample. Capture left and right next.';
		} catch (err) {
			error = err instanceof Error ? err.message : 'Could not read the profile photo.';
		}
	}

	function imageFileToFaceDataUrl(file: File): Promise<string> {
		return new Promise((resolve, reject) => {
			const url = URL.createObjectURL(file);
			const img = new Image();
			img.onload = () => {
				try {
					const canvas = document.createElement('canvas');
					canvas.width = 220;
					canvas.height = 260;
					const ctx = canvas.getContext('2d');
					if (!ctx) throw new Error('Could not prepare the profile photo.');
					const sourceSide = Math.min(img.naturalWidth || img.width, img.naturalHeight || img.height);
					const sourceX = Math.max(0, ((img.naturalWidth || img.width) - sourceSide) / 2);
					const sourceY = Math.max(0, ((img.naturalHeight || img.height) - sourceSide) / 2);
					ctx.fillStyle = '#030712';
					ctx.fillRect(0, 0, canvas.width, canvas.height);
					ctx.save();
					ctx.beginPath();
					ctx.ellipse(canvas.width / 2, canvas.height / 2, canvas.width * 0.5, canvas.height * 0.5, 0, 0, Math.PI * 2);
					ctx.clip();
					ctx.drawImage(img, sourceX, sourceY, sourceSide, sourceSide, 0, 0, canvas.width, canvas.height);
					ctx.restore();
					let quality = 0.84;
					let dataUrl = canvas.toDataURL('image/jpeg', quality);
					while (decodedBase64Size(dataUrl) > 32 * 1024 && quality > 0.42) {
						quality -= 0.1;
						dataUrl = canvas.toDataURL('image/jpeg', quality);
					}
					resolve(dataUrl);
				} catch (err) {
					reject(err);
				} finally {
					URL.revokeObjectURL(url);
				}
			};
			img.onerror = () => {
				URL.revokeObjectURL(url);
				reject(new Error('Could not decode the profile photo.'));
			};
			img.src = url;
		});
	}

	function playCaptureClick() {
		try {
			const AudioContextCtor = window.AudioContext || (window as unknown as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
			if (!AudioContextCtor) return;
			const ctx = new AudioContextCtor();
			const osc = ctx.createOscillator();
			const gain = ctx.createGain();
			osc.type = 'square';
			osc.frequency.setValueAtTime(920, ctx.currentTime);
			osc.frequency.exponentialRampToValueAtTime(520, ctx.currentTime + 0.045);
			gain.gain.setValueAtTime(0.0001, ctx.currentTime);
			gain.gain.exponentialRampToValueAtTime(0.08, ctx.currentTime + 0.006);
			gain.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + 0.065);
			osc.connect(gain);
			gain.connect(ctx.destination);
			osc.start();
			osc.stop(ctx.currentTime + 0.07);
			window.setTimeout(() => ctx.close(), 120);
		} catch {
			// Cosmetic feedback only.
		}
	}

	function blobToBase64(blob: Blob): Promise<string> {
		return new Promise((resolve, reject) => {
			const reader = new FileReader();
			reader.onload = () => resolve(String(reader.result || '').split(',', 2)[1] || '');
			reader.onerror = () => reject(reader.error);
			reader.readAsDataURL(blob);
		});
	}

	async function recordVoiceSample(durationMs = 6200): Promise<VoiceSample> {
		if (!stream) throw new Error('No microphone stream is available.');
		const audioTracks = stream.getAudioTracks();
		if (!audioTracks.length) throw new Error('No microphone track is available.');
		const recordingStream = new MediaStream(audioTracks);
		recordingChunks = [];
		const options = MediaRecorder.isTypeSupported('audio/webm;codecs=opus')
			? { mimeType: 'audio/webm;codecs=opus', audioBitsPerSecond: 16000 }
			: { audioBitsPerSecond: 16000 };
		return new Promise((resolve, reject) => {
			mediaRecorder = new MediaRecorder(recordingStream, options);
			mediaRecorder.ondataavailable = (event) => {
				if (event.data.size > 0) recordingChunks.push(event.data);
			};
			mediaRecorder.onerror = () => reject(new Error('Unable to record voice sample.'));
			mediaRecorder.onstop = async () => {
				try {
					const mimeType = mediaRecorder?.mimeType || 'audio/webm';
					const blob = new Blob(recordingChunks, { type: mimeType });
					const encoded = await blobToBase64(blob);
					if (decodedBase64Size(encoded) > 64 * 1024) {
						reject(new Error('Voice sample is too large. Please try again closer to the microphone.'));
						return;
					}
					const { transcript, warnings } = await transcribeVoiceSample(encoded, mimeType);
					resolve({ base64: encoded, mimeType, transcript, warnings });
				} catch (err) {
					reject(err instanceof Error ? err : new Error('Unable to prepare voice sample.'));
				} finally {
					isRecording = false;
				}
			};
			isRecording = true;
			mediaRecorder.start();
			window.setTimeout(() => {
				if (mediaRecorder?.state === 'recording') mediaRecorder.stop();
			}, durationMs);
		});
	}

	async function recordVoice() {
		if (!stream) return;
		error = '';
		status = `Recording. Please say: "${voicePhrase}"`;
		capturedVoiceBase64 = '';
		capturedVoiceMimeType = '';
		heardVoiceText = '';
		try {
			const sample = await recordVoiceSample();
			capturedVoiceBase64 = sample.base64;
			capturedVoiceMimeType = sample.mimeType;
			heardVoiceText = sample.transcript;
			status = sample.transcript ? 'Voice sample captured and transcribed' : 'Voice sample captured';
		} catch (err) {
			error = err instanceof Error ? err.message : 'Unable to prepare voice sample.';
		}
	}

	async function transcribeVoiceSample(base64: string, mimeType: string): Promise<{ transcript: string; warnings: string[] }> {
		try {
			const response = await fetch('./guardian/v1/knowledge/transcribe', {
				method: 'POST',
				headers: { 'content-type': 'application/json' },
				body: JSON.stringify({
					namespace,
					modality: 'audio',
					mime_type: mimeType,
					content_base64: base64
				})
			});
			const payload = await response.json().catch(() => ({}));
			return {
				transcript: typeof payload?.transcript === 'string' ? payload.transcript.trim() : '',
				warnings: Array.isArray(payload?.warnings) ? payload.warnings : []
			};
		} catch {
			return { transcript: '', warnings: ['asr_request_failed'] };
		}
	}

	function startFaceAutoCapture() {
		if (faceAutoTimer !== undefined) window.clearInterval(faceAutoTimer);
		faceStableFrames = 0;
		facePosePromptUntil = Date.now() + 2600;
		faceQuality = `${activeFace.cue}. Get ready...`;
		faceQualityState = 'searching';
		faceAutoTimer = window.setInterval(evaluateFaceForAutoCapture, 650);
	}

	async function evaluateFaceForAutoCapture() {
		if (faceAutoBusy || modality !== 'face' || !videoRef || !stream || capturedFaces[activeFaceAngle]) return;
		if (!videoRef.videoWidth || !videoRef.videoHeight) {
			faceQuality = 'Waiting for camera frames...';
			return;
		}
		if (Date.now() < facePosePromptUntil) {
			faceStableFrames = 0;
			faceQualityState = 'searching';
			faceQuality = `${activeFace.cue}. Get ready...`;
			return;
		}
		faceAutoBusy = true;
		try {
			const placement = await browserFacePlacement();
			if (placement.ok) {
				faceStableFrames += 1;
				faceQualityState = 'ready';
				const remaining = Math.max(1, 5 - faceStableFrames);
				faceQuality = faceStableFrames >= 2 ? `Hold still... ${remaining}` : placement.message;
			} else {
				faceStableFrames = 0;
				faceQualityState = placement.state;
				faceQuality = placement.message;
			}
			if (faceStableFrames >= 5) {
				await captureFace();
			}
		} finally {
			faceAutoBusy = false;
		}
	}

	async function browserFacePlacement(): Promise<{ ok: boolean; message: string; state: 'searching' | 'aligning' | 'ready' }> {
		const gatewayPlacement = await gatewayFacePlacement();
		if (gatewayPlacement) return gatewayPlacement;

		type FaceLandmark = { type?: string; locations?: { x: number; y: number }[] };
		type DetectedFace = { boundingBox: DOMRectReadOnly; landmarks?: FaceLandmark[] };
		const detectorCtor =
			typeof window !== 'undefined'
				? (window as unknown as {
						FaceDetector?: new () => { detect: (source: HTMLVideoElement) => Promise<DetectedFace[]> };
					}).FaceDetector
				: undefined;
		try {
			if (detectorCtor) {
				const faces = await new detectorCtor().detect(videoRef as HTMLVideoElement);
				if (faces.length !== 1) {
					return {
						ok: false,
						message: faces.length > 1 ? 'One face only, please' : 'Move your face into the guide',
						state: 'searching'
					};
				}
				const box = faces[0].boundingBox;
				const width = videoRef?.videoWidth || 1;
				const height = videoRef?.videoHeight || 1;
				const cx = (box.x + box.width / 2) / width;
				const cy = (box.y + box.height / 2) / height;
				const faceHeight = box.height / height;
				if (cx < 0.35) return { ok: false, message: 'Move slightly right', state: 'aligning' };
				if (cx > 0.65) return { ok: false, message: 'Move slightly left', state: 'aligning' };
				if (cy < 0.28) return { ok: false, message: 'Move slightly down', state: 'aligning' };
				if (cy > 0.72) return { ok: false, message: 'Move slightly up', state: 'aligning' };
				if (faceHeight < 0.24) return { ok: false, message: 'Move closer', state: 'aligning' };
				if (faceHeight > 0.72) return { ok: false, message: 'Move back a little', state: 'aligning' };
				const eyeLandmarks = (faces[0].landmarks || []).filter((landmark) =>
					String(landmark.type || '').toLowerCase().includes('eye')
				);
				if (eyeLandmarks.length >= 2) return { ok: true, message: 'Eyes aligned', state: 'ready' };
				return { ok: true, message: 'Face position good', state: 'ready' };
			}
			return browserEyeHeuristicPlacement();
		} catch {
			return browserEyeHeuristicPlacement();
		}
	}

	async function gatewayFacePlacement(): Promise<{ ok: boolean; message: string; state: 'searching' | 'aligning' | 'ready' } | null> {
		if (!videoRef || !canvasRef) return null;
		try {
			const frame = currentPoseFrameBase64();
			if (!frame) return null;
			const response = await fetch('./guardian/v1/perception/face/pose', {
				method: 'POST',
				headers: { 'content-type': 'application/json' },
				body: JSON.stringify({
					requested_pose: activeFaceAngle,
					frame_base64: frame
				})
			});
			if (!response.ok) return null;
			const payload = await response.json();
			const state = ['searching', 'aligning', 'ready'].includes(payload?.state) ? payload.state : 'searching';
			return {
				ok: state === 'ready',
				message: typeof payload?.message === 'string' ? payload.message : 'Gateway checking pose...',
				state
			};
		} catch {
			return null;
		}
	}

	function currentPoseFrameBase64(): string {
		if (!videoRef || !canvasRef || !videoRef.videoWidth || !videoRef.videoHeight) return '';
		const width = 224;
		const height = 224;
		canvasRef.width = width;
		canvasRef.height = height;
		const ctx = canvasRef.getContext('2d');
		if (!ctx) return '';
		const sourceSide = Math.min(videoRef.videoWidth, videoRef.videoHeight);
		const sourceX = Math.max(0, (videoRef.videoWidth - sourceSide) / 2);
		const sourceY = Math.max(0, (videoRef.videoHeight - sourceSide) / 2);
		ctx.drawImage(videoRef, sourceX, sourceY, sourceSide, sourceSide, 0, 0, width, height);
		return canvasRef.toDataURL('image/jpeg', 0.58);
	}

	function browserEyeHeuristicPlacement(): { ok: boolean; message: string; state: 'searching' | 'aligning' | 'ready' } {
		if (!videoRef || !canvasRef) return { ok: false, message: 'Waiting for camera frames...', state: 'searching' };
		const width = 160;
		const height = 120;
		canvasRef.width = width;
		canvasRef.height = height;
		const ctx = canvasRef.getContext('2d', { willReadFrequently: true });
		if (!ctx) return { ok: false, message: 'Waiting for camera frames...', state: 'searching' };
		ctx.drawImage(videoRef, 0, 0, width, height);
		const data = ctx.getImageData(0, 0, width, height).data;
		const xMin = Math.floor(width * 0.25);
		const xMax = Math.floor(width * 0.75);
		const yMin = Math.floor(height * 0.24);
		const yMax = Math.floor(height * 0.54);
		let leftCount = 0;
		let rightCount = 0;
		let leftX = 0;
		let leftY = 0;
		let rightX = 0;
		let rightY = 0;
		for (let y = yMin; y < yMax; y += 1) {
			for (let x = xMin; x < xMax; x += 1) {
				const offset = (y * width + x) * 4;
				const r = data[offset];
				const g = data[offset + 1];
				const b = data[offset + 2];
				const luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
				if (luminance > 72) continue;
				if (x < width / 2) {
					leftCount += 1;
					leftX += x;
					leftY += y;
				} else {
					rightCount += 1;
					rightX += x;
					rightY += y;
				}
			}
		}
		if (leftCount < 18 || rightCount < 18) {
			return { ok: false, message: 'Line up both eyes or glasses inside the guide', state: 'searching' };
		}
		const lx = leftX / leftCount;
		const rx = rightX / rightCount;
		const ly = leftY / leftCount;
		const ry = rightY / rightCount;
		const midX = (lx + rx) / 2 / width;
		const eyeLevel = (ly + ry) / 2 / height;
		const eyeSpread = (rx - lx) / width;
		if (Math.abs(ly - ry) > height * 0.08) return { ok: false, message: 'Keep your eyes level', state: 'aligning' };
		if (midX < 0.42) return { ok: false, message: 'Move slightly right', state: 'aligning' };
		if (midX > 0.58) return { ok: false, message: 'Move slightly left', state: 'aligning' };
		if (eyeLevel < 0.28) return { ok: false, message: 'Move slightly down', state: 'aligning' };
		if (eyeLevel > 0.48) return { ok: false, message: 'Move slightly up', state: 'aligning' };
		if (eyeSpread < 0.12) return { ok: false, message: 'Move closer', state: 'aligning' };
		if (eyeSpread > 0.42) return { ok: false, message: 'Move back a little', state: 'aligning' };
		return { ok: true, message: 'Eyes or glasses aligned', state: 'ready' };
	}

	async function submitEnrollment() {
		const cleanName = name.trim();
		if (!cleanName) {
			error = 'Enter the name this identity belongs to.';
			return;
		}
		if (modality === 'face' && capturedFaceCount < faceAngles.length) {
			error = 'Capture front, left, and right face samples first.';
			return;
		}
		if (modality === 'voice' && !capturedVoiceBase64) {
			error = 'Record a voice sample first.';
			return;
		}
		const faceBase64 = capturedFaces.front.split(',', 2)[1] || '';
		if (modality === 'face' && !faceBase64) {
			error = 'Capture the front face sample first.';
			return;
		}

		isSubmitting = true;
		error = '';
		try {
			const payload =
				modality === 'face'
					? {
							approved: true,
							source: 'svelte_chat_identity_enrollment',
							namespace,
							name: cleanName,
							modality: capturedVoiceBase64 ? 'face_voice' : 'face',
							face_seed: faceSeed(cleanName),
							face_thumbnail_base64: capturedFaces.front,
							face_left_thumbnail_base64: capturedFaces.left,
							face_right_thumbnail_base64: capturedFaces.right,
							...(capturedVoiceBase64 ? { voice_template: capturedVoiceBase64 } : {}),
							...(capturedVoiceMimeType ? { voice_mime_type: capturedVoiceMimeType } : {}),
							...(heardVoiceText ? { voice_transcript: heardVoiceText } : {}),
							face_bbox: [0, 0, 1, 1]
						}
					: {
							approved: true,
							source: 'svelte_chat_identity_enrollment',
							namespace,
							name: cleanName,
							modality,
							voice_template: capturedVoiceBase64,
							voice_mime_type: capturedVoiceMimeType,
							voice_transcript: heardVoiceText
						};
			const response = await fetch('./guardian/v1/perception/identity/enroll', {
				method: 'POST',
				headers: { 'content-type': 'application/json' },
				body: JSON.stringify(payload)
			});
			const text = await response.text();
			if (!response.ok) throw new Error(text || `Enrollment failed (${response.status})`);
			status = `${modality === 'face' ? 'Face' : 'Voice'} enrolled for ${cleanName}`;
			window.setTimeout(() => handleOpenChange(false), 900);
		} catch (err) {
			error = err instanceof Error ? err.message : 'Enrollment failed.';
		} finally {
			isSubmitting = false;
		}
	}

	function simpleHash(value: string): string {
		let hash = 2166136261;
		for (let index = 0; index < value.length; index++) {
			hash ^= value.charCodeAt(index);
			hash = Math.imul(hash, 16777619);
		}
		return (hash >>> 0).toString(16).padStart(8, '0');
	}

	function faceSeed(cleanName: string): string {
		return [
			namespace,
			cleanName,
			...faceAngles.map((angle) => `${angle.key}:${simpleHash(capturedFaces[angle.key])}`)
		].join('|');
	}

	onDestroy(stopCapture);
</script>

<Dialog.Root bind:open onOpenChange={handleOpenChange}>
	<Dialog.Content class="max-h-[94dvh] overflow-hidden p-0 sm:max-w-[52rem]">
		<div class="grid max-h-[94dvh] overflow-y-auto bg-background">
			<div class="grid gap-3 border-b border-border/70 p-4">
				<div class="flex items-center justify-between gap-3">
					<Dialog.Title class="flex items-center gap-2 text-base">
						{#if modality === 'face'}
							<Camera class="h-5 w-5" />
						{:else}
							<Mic class="h-5 w-5" />
						{/if}
						{title}
					</Dialog.Title>
					<label class="flex min-w-0 flex-1 items-center justify-end gap-2 text-xs text-muted-foreground">
						<span>Name</span>
						<Input bind:value={name} placeholder="Iain" autocomplete="name" class="h-8 max-w-44" />
					</label>
				</div>
				{#if modality === 'voice' || (modality === 'face' && faceStreamHasAudio)}
					<div class="grid gap-1">
						<div class="text-xs font-medium uppercase tracking-wide text-muted-foreground">Say this</div>
						<div class="rounded-lg border border-cyan-500/25 bg-cyan-500/10 px-3 py-2 font-mono text-sm text-foreground">
							{voicePhrase}
						</div>
						<div class="min-h-9 rounded-lg border border-border/70 bg-muted/30 px-3 py-2 text-sm">
							{#if heardVoiceText}
								<span class="text-muted-foreground">Heard: </span>{heardVoiceText}
							{:else}
								<span class="text-muted-foreground">Heard words will appear here after recording.</span>
							{/if}
						</div>
					</div>
				{/if}
			</div>

			<div class="relative min-h-[34rem] bg-zinc-950">
				<div class="absolute inset-0 bg-[radial-gradient(circle_at_30%_20%,rgba(20,184,166,0.35),transparent_34%),radial-gradient(circle_at_80%_72%,rgba(59,130,246,0.28),transparent_32%)]"></div>
				{#if modality === 'face'}
					{#if stream}
						<video
							bind:this={videoRef}
							class="relative z-10 h-full min-h-[34rem] w-full object-cover"
							autoplay
							muted
							playsinline
						></video>
						<div class="pointer-events-none absolute inset-0 z-20 grid place-items-center">
							<div class="absolute inset-0 backdrop-blur-[5px] [mask-image:radial-gradient(ellipse_30%_36%_at_50%_50%,transparent_0_67%,black_69%)]"></div>
							<div class="relative z-10 h-[68%] max-h-80 w-[58%] min-w-44 max-w-64 rounded-[48%] border-2 transition-all duration-200 {guideClass}">
								<div class="absolute top-[38%] left-1/2 h-px w-[112%] -translate-x-1/2 bg-cyan-100/35"></div>
								<div class="absolute top-1/2 left-1/2 h-[112%] w-px -translate-x-1/2 -translate-y-1/2 bg-cyan-100/35"></div>
							</div>
						</div>
						<div class="absolute top-4 right-4 z-30 grid w-28 gap-2">
							{#each faceAngles as angle}
								<button
									type="button"
									onclick={() => (activeFaceAngle = angle.key)}
									class="pointer-events-auto relative h-24 overflow-hidden rounded-lg border text-xs shadow-xl backdrop-blur transition {capturedFaces[angle.key]
										? 'border-emerald-300/80 bg-zinc-950 text-white'
										: activeFaceAngle === angle.key
											? 'border-cyan-300 bg-cyan-950/55 text-white'
											: 'border-white/20 bg-black/45 text-white/80'}"
								>
									{#if capturedFaces[angle.key]}
										<img
											src={capturedFaces[angle.key]}
											alt="{angle.cue} captured face sample"
											class="h-full w-full object-cover"
										/>
										<span class="absolute inset-x-0 bottom-0 bg-black/65 px-2 py-1 font-medium text-white backdrop-blur">
											{angle.label}
										</span>
									{:else}
										<span class="absolute inset-x-0 top-1/2 -translate-y-1/2 px-2 font-medium">
											{angle.label}
										</span>
										<span class="absolute inset-x-0 bottom-1 px-2 text-[0.65rem] text-white/70">
											{activeFaceAngle === angle.key ? 'ready' : 'waiting'}
										</span>
									{/if}
								</button>
							{/each}
						</div>
						{#if previousFaceCaptures.length}
							<div class="absolute bottom-4 left-4 z-30 grid max-w-[19rem] gap-2 rounded-xl border border-white/15 bg-black/45 p-2 text-white shadow-2xl backdrop-blur">
								<div class="flex items-center justify-between gap-3 px-1 text-[0.65rem] font-medium uppercase tracking-wide text-white/75">
									<span>Previous</span>
									<span class="truncate normal-case tracking-normal text-white/60">
										{previousFaceCaptures[0].name}
									</span>
								</div>
								<div class="grid grid-cols-3 gap-2">
									{#each [
										{ label: 'Front', src: previousFaceCaptures[0].front },
										{ label: 'Left', src: previousFaceCaptures[0].left },
										{ label: 'Right', src: previousFaceCaptures[0].right }
									] as previous}
										<div class="relative h-20 overflow-hidden rounded-lg border border-white/15 bg-black/35">
											{#if previous.src}
												<img src={previous.src} alt="Previous {previous.label} face capture" class="h-full w-full object-cover" />
											{:else}
												<div class="grid h-full place-items-center text-[0.65rem] text-white/45">none</div>
											{/if}
											<span class="absolute inset-x-0 bottom-0 bg-black/65 px-1 py-0.5 text-center text-[0.62rem] font-medium text-white backdrop-blur">
												{previous.label}
											</span>
										</div>
									{/each}
								</div>
								{#if previousFaceCaptures.length > 1}
									<div class="px-1 text-[0.62rem] text-white/55">
										+{previousFaceCaptures.length - 1} older set{previousFaceCaptures.length === 2 ? '' : 's'}
									</div>
								{/if}
							</div>
						{/if}
					{:else}
						<div class="relative z-10 flex h-full min-h-[34rem] flex-col items-center justify-center gap-5 px-10 text-center text-white">
							<div class="flex h-24 w-24 items-center justify-center rounded-full border border-white/25 bg-white/10 shadow-2xl backdrop-blur">
								<Camera class="h-10 w-10" />
							</div>
							<div class="max-w-sm text-sm text-white/80">
								The browser needs a direct permission click before it can open your camera and microphone.
							</div>
							{#if permissionHint}
								<div class="max-w-sm rounded-lg border border-white/15 bg-white/10 px-4 py-3 text-xs text-white/85 backdrop-blur">
									{permissionHint}
								</div>
							{/if}
							<Button onclick={startCapture} disabled={isStarting} class="bg-white text-zinc-950 hover:bg-white/90">
								<Camera class="h-4 w-4" />
								{isStarting ? 'Opening...' : hasRememberedMediaGrant() ? 'Start camera and microphone' : 'Allow camera and microphone'}
							</Button>
							{#if typeof window !== 'undefined' && !isTrustedMediaOrigin()}
								<a class="text-xs font-medium text-cyan-100 underline underline-offset-4" href={httpsHref()}>
									Try HTTPS for this page
								</a>
							{/if}
						</div>
					{/if}
				{:else}
					<div class="relative z-10 flex h-full min-h-[34rem] flex-col items-center justify-center gap-5 px-10 text-center text-white">
						<div class="flex h-28 w-28 items-center justify-center rounded-full border border-white/25 bg-white/10 shadow-2xl backdrop-blur">
							<Mic class="h-12 w-12 {isRecording ? 'text-red-300' : 'text-white'}" />
						</div>
						<div class="h-2 w-56 overflow-hidden rounded-full bg-white/15">
							<div
								class="h-full rounded-full bg-cyan-300 transition-all {isRecording ? 'w-full duration-[4200ms]' : capturedVoiceBase64 ? 'w-full' : 'w-2'}"
							></div>
						</div>
						{#if !stream}
							<Button onclick={startCapture} disabled={isStarting} class="bg-white text-zinc-950 hover:bg-white/90">
								<Mic class="h-4 w-4" />
								{isStarting ? 'Opening...' : hasRememberedMediaGrant() ? 'Start microphone' : 'Allow microphone'}
							</Button>
						{/if}
					</div>
				{/if}
				<canvas bind:this={canvasRef} class="hidden"></canvas>
			</div>

			<div class="grid gap-3 p-4">
				{#if modality === 'face'}
					<input
						bind:this={profilePhotoInput}
						type="file"
						accept="image/*"
						class="hidden"
						onchange={handleProfilePhotoSelected}
					/>
				{/if}
				{#if error}
					<div class="rounded-md border border-destructive/30 bg-destructive/10 px-3 py-2 text-sm text-destructive">
						{error}
					</div>
				{/if}

				<Dialog.Footer class="gap-2 sm:justify-between">
					<Button variant="outline" onclick={startCapture} disabled={isStarting || isSubmitting}>
						<RefreshCw class="h-4 w-4 {isStarting ? 'animate-spin' : ''}" />
						{permissionPrompted ? 'Reset' : hasRememberedMediaGrant() ? 'Start' : 'Allow'}
					</Button>
					<div class="flex flex-wrap justify-end gap-2">
						{#if modality === 'face'}
							<Button variant="secondary" onclick={() => profilePhotoInput?.click()} disabled={isSubmitting}>
								<ImagePlus class="h-4 w-4" />
								Use profile photo
							</Button>
							<Button onclick={recordVoice} disabled={!stream || !faceStreamHasAudio || isRecording || isSubmitting}>
								<Mic class="h-4 w-4" />
								{isRecording ? 'Recording' : capturedVoiceBase64 ? 'Voice ready' : 'Record phrase'}
							</Button>
						{:else}
							<Button onclick={recordVoice} disabled={!stream || isRecording || isSubmitting}>
								<Mic class="h-4 w-4" />
								{isRecording ? 'Recording' : 'Record phrase'}
							</Button>
						{/if}
						<Button onclick={submitEnrollment} disabled={isSubmitting || isRecording}>
							<ShieldCheck class="h-4 w-4" />
							{modality === 'face' ? `Enroll ${capturedFaceCount}/3` : 'Enroll'}
						</Button>
					</div>
				</Dialog.Footer>
			</div>
		</div>
	</Dialog.Content>
</Dialog.Root>
